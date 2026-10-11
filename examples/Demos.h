#ifndef COMMONUTILS_EXAMPLES_DEMOS_H_
#define COMMONUTILS_EXAMPLES_DEMOS_H_

// Each demo prints what it's doing as it goes, so this doubles as a guided
// tour of the library -- run the built example and read along.

void RunLoggerDemo();
void RunCircularBufferDemo();
void RunTimerDemo();
void RunSnoozableTimerDemo();
void RunDataHandlerDemo();

// triggerRealCrash: when true, deliberately dereferences a null pointer
// after installing StackTrace's crash handlers, so you can see the real
// crash-handling path fire (this terminates the process, by design -- it's
// what StackTrace::installSignalHandlers() is for). When false (the
// default), only the non-crash captureStackTrace() API is demonstrated.
void RunStackTraceDemo(bool triggerRealCrash);

#endif // COMMONUTILS_EXAMPLES_DEMOS_H_
