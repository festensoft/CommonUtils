# SPDX-License-Identifier: BSL-1.0
# Copyright 2026 Steven Festenstein

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
