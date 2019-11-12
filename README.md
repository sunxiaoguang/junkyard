junkyard
========
lsof.exe

cl /EHsc lsof.cc advapi32.lib psapi.lib
  
lsof.exe <execname> execname is optional, when it's empty, list all processes and their open files in XML format. when it's given, only processes has the same execname will be listed. output is XML format to make parsing easier.

