cmake_minimum_required(VERSION 3.5)

project(NatTypeProb)

set(CMAKE_CXX_STANDARD 11)
 
aux_source_directory(src FILE)
  
add_library(NatTypeProb STATIC ${FILE})

target_include_directories(NatTypeProb PUBLIC 
	${PROJECT_SOURCE_DIR}/include
)