# stopwatch-DE1-SOC

  Kernel Driver implemented on DE1-SOC for stopwatch
  
  Supports "stop", "run" for stopwatch. "disp", and "nodisp" for display of 7-Segment displays.
  
  MINUTE:SECONDS:HUNDREDTH_SECOND in form of (MM:SS:DD) to module to set the timer

  Read returns the current time
  
  Current time is displayed on (6) 7-Segment Displays
  
  User.c
  
  Pressing KEY0 should toggle the stopwatch between the run and pause states. 
  Pressing KEY1 to KEY3 should set the time according to the values of the SW slider 
  switches. Set the hundredths (DD) if KEY1 is pressed, the seconds (SS) for KEY2, 
  and the minutes (MM) for KEY3.
