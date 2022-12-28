TARGETS = proto kvstore
OUTDIR = ../common/proto_gen
DIR = ../common

all: $(TARGETS)

proto : $(DIR)/proto.proto
	mkdir -p $(OUTDIR)
	protoc -I=$(DIR) --cpp_out=$(OUTDIR) $(DIR)/proto.proto

kvstore : kvstore.cpp proto
	pkg-config --cflags protobuf
	c++ $(CFLAGS) -std=c++17 kvstore.cpp $(OUTDIR)/proto.pb.cc -o kvstore `pkg-config --cflags --libs protobuf`

clean::
	rm -fv $(TARGETS) *~ *.o $(OUTDIR)/*