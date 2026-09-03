# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "debug")
  file(REMOVE_RECURSE
  "MCXA153_Touch_Audio_Workstation.bin"
  "clean_files-NOTFOUND"
  )
endif()
