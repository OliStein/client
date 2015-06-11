SRC = Client.cpp
OBJ = $(SRC:.cpp=.o)
EXECUTABLE = Client

# include directories

INCLUDES = -I. -I/cvmfs/sft.cern.ch/lcg/external/Boost/1.53.0_python2.7/x86_64-slc6-gcc48-opt/include/boost-1_53/

# C++ compiler flags (-g -O2 -Wall)
CCFLAGS = -g -Wall -stdlib=libc++

# compiler
GCC = g++

# library paths 
LIBS = -L. -L/cvmfs/sft.cern.ch/lcg/external/Boost/1.53.0_python2.7/x86_64-slc6-gcc48-opt/lib -lm -lpthread -lboost_system-gcc48-mt-1_53 -lboost_thread-gcc48-mt-1_53 -lboost_timer-gcc48-mt-1_53 -lboost_chrono-gcc48-mt-1_53 

# compile flags
LDFLAGS = -g 

all: Client.o Client

Client: $(OBJ)
	$(CC) $(CCFLAGS) $(OBJ) -o $(EXECUTABLE) $(LIBS)

Client.o:  Client.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c Client.cpp -o Client.o $(LIBS)

clean:
	 rm ./*.o Client
