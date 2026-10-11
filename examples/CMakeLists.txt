add_executable(CommonUtilsExample
   main.cpp
   LoggerDemo.cpp
   CircularBufferDemo.cpp
   TimerDemo.cpp
   SnoozableTimerDemo.cpp
   DataHandlerDemo.cpp
   StackTraceDemo.cpp
)

target_link_libraries(CommonUtilsExample PRIVATE CommonUtils CommonUtils_warnings)
