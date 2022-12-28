TARGETS = proto smtp
DIR = ../common
OUTDIR = ../common/proto_gen

all: $(TARGETS)

proto : $(DIR)/proto.proto
	mkdir -p $(OUTDIR)
	protoc -I=$(DIR) --cpp_out=$(OUTDIR) $(DIR)/proto.proto

smtp : smtp.cpp proto
	pkg-config --cflags protobuf
	c++ $(CFLAGS) -std=c++17 smtp.cpp $(OUTDIR)/proto.pb.cc  -o smtp -lresolv `pkg-config --cflags --libs protobuf`
	
clean::
	rm -fv $(TARGETS) *~ *.o $(OUTDIR)/*
