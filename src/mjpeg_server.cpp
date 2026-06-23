#include "mjpeg_server.hpp"

#include <App.h>
#include <HttpResponse.h>
#include <Loop.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "constants.hpp"
#include "http_response_builder.hpp"
#include "video_frame_buffer.hpp"
#include "video_frame_fragment.hpp"

MjpegServer::MjpegServer(const int port, const std::atomic<bool>& running, std::string_view index_html,
                         VideoFrameBuffer& disruptor)
    : port_(port), running_(running), index_html_(index_html), disruptor_(&disruptor) {}

MjpegServer::~MjpegServer() {
    if (networkThread_.joinable()) {
        networkThread_.join();
    }
    std::cout << "[Network Core] Network engine cleanly terminated.\n";
}

void MjpegServer::onTimer(us_timer_t* t) {
    auto* server = *static_cast<MjpegServer**>(us_timer_ext(t));

    if (!server->running_) {
        for (auto& viewer : server->activeViewers_) {
            if (!viewer.isClosed) {
                viewer.res->close();
            }
        }
        server->activeViewers_.clear();

        if (server->listenSocket_) {
            constexpr int GRACEFUL_CLOSE = 0;
            us_listen_socket_close(GRACEFUL_CLOSE, server->listenSocket_);
            server->listenSocket_ = nullptr;
        }
        us_timer_close(t);
        return;
    }

    int64_t available = server->disruptor_->getHighestPublished();
    if (available < server->nextReadSequence_) {
        return;
    }
    bool processedAny = false;

    while (server->nextReadSequence_ <= available) {
        VideoFrameFragment& currentFrame =
            server->disruptor_->getBySequence(server->nextReadSequence_);

        const uint32_t currentFrameId = static_cast<uint32_t>(server->nextReadSequence_);

        if (currentFrame.contentSize() > 0) {
            for (size_t i = 0; i < server->activeViewers_.size();) {
                auto& viewer = server->activeViewers_[i];
                auto* res = viewer.res;

                if (viewer.isClosed) {
                    ++i;
                    continue;
                }

                if (res->getWriteOffset() > WebServerConfig::MAX_OUTGOING_CLIENT_BUFFER_SIZE) {
                    std::cerr << "[Network Core] Evicting lagging viewer on /stream.\n";
                    res->end();
                    viewer.isClosed = true;
                    ++i;
                    continue;
                }

                if (viewer.lastSentFrameId < currentFrameId) {
                    size_t backpressure = res->getWriteOffset();

                    if (backpressure == 0) {
                        if (viewer.isLagging) {
                            uint32_t droppedFrames = currentFrameId - viewer.lagStartFrameId;
                            std::cerr << "[Network Telemetry] Viewer recovered. TCP pipe cleared. "
                                      << droppedFrames
                                      << " frames were deliberately dropped to maintain real-time "
                                         "latency.\n";
                            viewer.isLagging = false;
                        }

                        CorkState state{res, HttpResponseBuilder::build(currentFrame), false};

                        state.res->cork([&state]() { state.ok = state.res->write(state.payload); });

                        if (!state.ok) {
                            std::cerr << "[Network Telemetry] ALERT: Kernel buffer rejected data! "
                                      << "uWebSockets just executed a user-space malloc to queue "
                                         "this frame.\n";
                        }

                        size_t postWriteBackpressure = res->getWriteOffset();
                        if (postWriteBackpressure > 0) {
                            std::cerr << "[Network Telemetry] HEAP ALLOCATION DETECTED! "
                                         "res->write() caused "
                                      << postWriteBackpressure
                                      << " bytes to be queued on the heap for viewer frame "
                                      << currentFrameId << "\n";
                        }
                    } else {
                        if (!viewer.isLagging) {
                            std::cerr << "[Network Telemetry] Warning: TCP stall detected! OS "
                                         "buffer backed up with "
                                      << backpressure << " bytes. Dropping frames...\n";
                            viewer.isLagging = true;
                            viewer.lagStartFrameId = currentFrameId;
                        }
                    }

                    viewer.lastSentFrameId = currentFrameId;
                }
                ++i;
            }
        }

        server->nextReadSequence_++;
        processedAny = true;
    }

    if (processedAny) {
        server->disruptor_->markConsumed(server->nextReadSequence_ - 1);
    }

    std::erase_if(server->activeViewers_, [](const auto& viewer) { return viewer.isClosed; });
}

void MjpegServer::start() {
    std::promise<void> loopPromise;
    auto loopFuture = loopPromise.get_future();

    networkThread_ = std::thread([this, &loopPromise]() {
        auto app = uWS::App();
        app.get("/", [this](auto* res, auto*) {
            res->writeHeader("Connection", "close")
                ->writeHeader("Content-Type", "text/html")
                ->end(index_html_);
        });
        app.get("/favicon.ico", [](auto* res, auto*) {
            res->writeStatus("404 Not Found")
                ->writeHeader("Connection", "close")
                ->writeHeader("Cache-Control", "public, max-age=31536000")
                ->end();
        });
        app.get("/stream", [this](auto* res, auto*) {
            if (activeViewers_.size() >= WebServerConfig::MAX_CLIENTS) {
                std::cerr << "Server at capacity. Refused new viewer\n";
                res->writeStatus("503 Service Unavailable")->end("Server Capacity Reached");
                return;
            }

            std::cerr << "Viewer connected to stream\n";

            res->writeStatus("200 OK")
                // Investigating test issues.
                //->writeHeader("Connection", "close")
                ->writeHeader("Cache-Control", "no-cache, private")
                ->writeHeader("Pragma", "no-cache")
                ->writeHeader("Content-Type", "multipart/x-mixed-replace; boundary=mjpegstream");

            activeViewers_.push_back({res, 0, false});

            res->onAborted([this, res]() {
                std::cerr << "Viewer disconnected from stream\n";

                auto it = std::find_if(activeViewers_.begin(), activeViewers_.end(),
                                       [res](const ViewerState& v) { return v.res == res; });

                if (it != activeViewers_.end()) {
                    it->isClosed = true;
                }
            });
        });
        app.any("/*", [](auto* res, auto*) { res->writeStatus("404 Not Found")->end(); });
        app.listen(port_, [this, &loopPromise](us_listen_socket_t* socket) {
            if (socket) {
                listenSocket_ = socket;
                std::cerr << "[Network Core] Asynchronous uWebSockets engine listening on port "
                          << port_ << '\n';

                auto* loop = reinterpret_cast<struct us_loop_t*>(uWS::Loop::get());
                us_timer_t* timer =
                    us_create_timer(loop, WebServerConfig::TIMER_FALLTHROUGH, sizeof(MjpegServer*));

                *static_cast<MjpegServer**>(us_timer_ext(timer)) = this;
                us_timer_set(timer, MjpegServer::onTimer, WebServerConfig::TIMER_INTERVAL_MS,
                             WebServerConfig::TIMER_INTERVAL_MS);

                loopPromise.set_value();
            } else {
                std::cerr << "[Network Core Error] Failed to bind to port " << port_ << '\n';
                try {
                    throw std::runtime_error("Port binding failed");
                } catch (...) {
                    loopPromise.set_exception(std::current_exception());
                }
            }
        });

        app.run();
    });

    loopFuture.wait();
}