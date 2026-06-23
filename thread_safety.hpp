#pragma once

// Enable Clang Thread Safety Analysis attributes.
// We must disable the macro-usage linter rule here because compiler attributes
// cannot be wrapped in standard C++ constexpr functions.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)

// Enable Clang Thread Safety Analysis attributes
#if defined(__clang__)
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)
#endif

// Class-level capability (identifies a class as a Mutex)
#define CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

// Data variable annotations
#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))
#define PT_GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

// Function requirement annotations
#define REQUIRES(...) THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))
#define REQUIRES_SHARED(...) THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

// Lock/Unlock annotations for custom wrappers
#define ACQUIRE(...) THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))
#define RELEASE(...) THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))
#define SCOPED_CAPABILITY THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

// NOLINTEND(cppcoreguidelines-macro-usage)