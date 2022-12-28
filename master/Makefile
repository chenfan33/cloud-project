TARGETS = proto master
OUTDIR = ../common/proto_gen
DIR = ../common

all: $(TARGETS)

proto : $(DIR)/proto.proto
	mkdir -p $(OUTDIR)
	protoc -I=$(DIR) --cpp_out=$(OUTDIR) $(DIR)/proto.proto

master : master.cpp proto
	pkg-config --cflags protobuf
	c++ $(CFLAGS) -std=c++14 master.cpp $(OUTDIR)/proto.pb.cc -o master `pkg-config --cflags --libs protobuf`

clean::
	rm -fv $(TARGETS) *~ *.o $(OUTDIR)/* 