/**
 * Autogenerated by Thrift Compiler (0.9.1)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#include "succinct_graph_types.h"

#include <algorithm>



const char* ThriftAssoc::ascii_fingerprint = "3CB6A5EF5FFCBCD18AE2151BFD483B57";
const uint8_t ThriftAssoc::binary_fingerprint[16] = {0x3C,0xB6,0xA5,0xEF,0x5F,0xFC,0xBC,0xD1,0x8A,0xE2,0x15,0x1B,0xFD,0x48,0x3B,0x57};

uint32_t ThriftAssoc::read(::apache::thrift::protocol::TProtocol* iprot) {

  uint32_t xfer = 0;
  std::string fname;
  ::apache::thrift::protocol::TType ftype;
  int16_t fid;

  xfer += iprot->readStructBegin(fname);

  using ::apache::thrift::protocol::TProtocolException;


  while (true)
  {
    xfer += iprot->readFieldBegin(fname, ftype, fid);
    if (ftype == ::apache::thrift::protocol::T_STOP) {
      break;
    }
    switch (fid)
    {
      case 1:
        if (ftype == ::apache::thrift::protocol::T_I64) {
          xfer += iprot->readI64(this->srcId);
          this->__isset.srcId = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 2:
        if (ftype == ::apache::thrift::protocol::T_I64) {
          xfer += iprot->readI64(this->dstId);
          this->__isset.dstId = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 3:
        if (ftype == ::apache::thrift::protocol::T_I64) {
          xfer += iprot->readI64(this->atype);
          this->__isset.atype = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 4:
        if (ftype == ::apache::thrift::protocol::T_I64) {
          xfer += iprot->readI64(this->timestamp);
          this->__isset.timestamp = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      case 5:
        if (ftype == ::apache::thrift::protocol::T_STRING) {
          xfer += iprot->readString(this->attr);
          this->__isset.attr = true;
        } else {
          xfer += iprot->skip(ftype);
        }
        break;
      default:
        xfer += iprot->skip(ftype);
        break;
    }
    xfer += iprot->readFieldEnd();
  }

  xfer += iprot->readStructEnd();

  return xfer;
}

uint32_t ThriftAssoc::write(::apache::thrift::protocol::TProtocol* oprot) const {
  uint32_t xfer = 0;
  xfer += oprot->writeStructBegin("ThriftAssoc");

  xfer += oprot->writeFieldBegin("srcId", ::apache::thrift::protocol::T_I64, 1);
  xfer += oprot->writeI64(this->srcId);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("dstId", ::apache::thrift::protocol::T_I64, 2);
  xfer += oprot->writeI64(this->dstId);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("atype", ::apache::thrift::protocol::T_I64, 3);
  xfer += oprot->writeI64(this->atype);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("timestamp", ::apache::thrift::protocol::T_I64, 4);
  xfer += oprot->writeI64(this->timestamp);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldBegin("attr", ::apache::thrift::protocol::T_STRING, 5);
  xfer += oprot->writeString(this->attr);
  xfer += oprot->writeFieldEnd();

  xfer += oprot->writeFieldStop();
  xfer += oprot->writeStructEnd();
  return xfer;
}

void swap(ThriftAssoc &a, ThriftAssoc &b) {
  using ::std::swap;
  swap(a.srcId, b.srcId);
  swap(a.dstId, b.dstId);
  swap(a.atype, b.atype);
  swap(a.timestamp, b.timestamp);
  swap(a.attr, b.attr);
  swap(a.__isset, b.__isset);
}


