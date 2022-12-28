TARGETS = proto frontend
DIR = ../common
OUTDIR = ../common/proto_gen

all: $(TARGETS)

proto : $(DIR)/proto.proto
	mkdir -p $(OUTDIR)
	protoc -I=$(DIR) --cpp_out=$(OUTDIR) $(DIR)/proto.proto

frontend : frontend.cpp proto
	pkg-config --cflags protobuf
	c++ $(CFLAGS) -std=c++17 frontend.cpp $(OUTDIR)/proto.pb.cc  -o frontend `pkg-config --cflags --libs protobuf` -lresolv

clean::
	rm -fv $(TARGETS) *~ *.o $(OUTDIR)/*
