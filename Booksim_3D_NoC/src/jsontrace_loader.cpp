// $Id$

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
 All rights reserved.
*/

#include "jsontrace_loader.hpp"

#include <cassert>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <sstream>

using namespace std;

// === JSON trace support: helper utilities ===============================

static string _Trim(string const & value)
{
  size_t start = 0;
  while((start < value.size()) && isspace(value[start])) {
    ++start;
  }
  size_t end = value.size();
  while(end > start && isspace(value[end - 1])) {
    --end;
  }
  if(end <= start) {
    return "";
  }
  return value.substr(start, end - start);
}

JsonTraceReader::JsonTraceReader(const string & trace_path,
				 const string & map_path,
				 long long limit,
				 unsigned int scale,
				 int nodes)
  : _trace_path(trace_path), _map_path(map_path), _limit(limit),
    _scale(scale ? scale : 1), _nodes(nodes), _stream(NULL),
    _has_map(false), _header_parsed(false), _partial_valid(false),
    _multicast_warning(false), _packets_read(0)
{
  if(_trace_path.empty()) {
    cerr << "JSON trace workload error: missing trace filename." << endl;
    exit(-1);
  }
  if(_nodes <= 0) {
    cerr << "JSON trace workload error: invalid node count." << endl;
    exit(-1);
  }
  _LoadMap();
  _OpenStream();
}

JsonTraceReader::~JsonTraceReader()
{
  _CloseStream();
}

void JsonTraceReader::_LoadMap()
{
  if(_map_path.empty()) {
    _has_map = false;
    return;
  }
  ifstream map_in(_map_path.c_str());
  if(!map_in.is_open()) {
    cerr << "Unable to open trace map file: " << _map_path << endl;
    exit(-1);
  }
  string contents((istreambuf_iterator<char>(map_in)),
		  istreambuf_iterator<char>());
  map_in.close();

  size_t pos = 0;
  bool found_entry = false;
  while(true) {
    pos = contents.find('"', pos);
    if(pos == string::npos) {
      break;
    }
    size_t end = contents.find('"', pos + 1);
    if(end == string::npos) {
      cerr << "Malformed map file (unterminated key)." << endl;
      exit(-1);
    }
    string key_str = contents.substr(pos + 1, end - pos - 1);
    pos = contents.find(':', end);
    if(pos == string::npos) {
      cerr << "Malformed map file (missing colon)." << endl;
      exit(-1);
    }
    ++pos;
    while((pos < contents.size()) && isspace(contents[pos])) {
      ++pos;
    }
    size_t val_end = pos;
    if((val_end < contents.size()) &&
       ((contents[val_end] == '-') || isdigit(contents[val_end]))) {
      ++val_end;
      while((val_end < contents.size()) && isdigit(contents[val_end])) {
	++val_end;
      }
    } else {
      cerr << "Malformed map file (missing integer value)." << endl;
      exit(-1);
    }
    string val_str = contents.substr(pos, val_end - pos);
    pos = val_end;
    int original = atoi(key_str.c_str());
    int mapped = atoi(val_str.c_str());
    if((mapped < 0) || (mapped >= _nodes)) {
      cerr << "Trace map value " << mapped
	   << " is outside the configured node count (" << _nodes << ")." << endl;
      exit(-1);
    }
    _id_map[original] = mapped;
    found_entry = true;
  }
  if(!found_entry) {
    cerr << "Trace map file did not contain any entries." << endl;
    exit(-1);
  }
  _has_map = true;
}

void JsonTraceReader::_OpenStream()
{
  _CloseStream();
  _stream = new ifstream(_trace_path.c_str());
  if(!_stream->is_open()) {
    cerr << "Unable to open JSON trace file: " << _trace_path << endl;
    exit(-1);
  }
  string header;
  if(!getline(*_stream, header)) {
    cerr << "JSON trace file is empty: " << _trace_path << endl;
    exit(-1);
  }
  header = _Trim(header);
  if(header.find("\"format\"") == string::npos) {
    cerr << "JSON trace header missing format identifier." << endl;
    exit(-1);
  }
  _header_parsed = true;
  _partial_valid = false;
  _queued_packets = queue<JsonTracePacket>();
  _packets_read = 0ULL;
}

void JsonTraceReader::_CloseStream()
{
  if(_stream) {
    if(_stream->is_open()) {
      _stream->close();
    }
    delete _stream;
    _stream = NULL;
  }
}

void JsonTraceReader::Reset()
{
  _OpenStream();
}

bool JsonTraceReader::limit_reached() const
{
  if(_limit < 0) {
    return false;
  }
  return (_packets_read >= (unsigned long long)_limit);
}

unsigned long long JsonTraceReader::_ScaleTime(long long raw) const
{
  if(raw < 0) {
    return 0;
  }
  if(_scale <= 1) {
    return (unsigned long long)raw;
  }
  return (unsigned long long)(raw / (long long)_scale);
}

int JsonTraceReader::_MapId(int original) const
{
  if(_has_map) {
    map<int, int>::const_iterator iter = _id_map.find(original);
    if(iter == _id_map.end()) {
      cerr << "Trace references unmapped node id " << original
	   << " (map file: " << _map_path << ")." << endl;
      exit(-1);
    }
    return iter->second;
  }
  if((original < 0) || (original >= _nodes)) {
    cerr << "Trace references node id " << original
	 << " which exceeds available nodes (" << _nodes << ")." << endl;
    if(!_map_path.empty()) {
      cerr << "Did you forget to include an entry for this node in "
	   << _map_path << "?" << endl;
    }
    exit(-1);
  }
  return original;
}

bool JsonTraceReader::_ExtractInt(string const & line, string const & key,
				  long long * value) const
{
  string token = "\"" + key + "\"";
  size_t pos = line.find(token);
  if(pos == string::npos) {
    return false;
  }
  pos = line.find(':', pos + token.size());
  if(pos == string::npos) {
    return false;
  }
  ++pos;
  while((pos < line.size()) && isspace(line[pos])) {
    ++pos;
  }
  size_t end = pos;
  if(end < line.size() && (line[end] == '-' || line[end] == '+')) {
    ++end;
  }
  while((end < line.size()) && isdigit(line[end])) {
    ++end;
  }
  if(end == pos) {
    return false;
  }
  string value_str = line.substr(pos, end - pos);
  *value = atoll(value_str.c_str());
  return true;
}

bool JsonTraceReader::_ExtractBool(string const & line, string const & key,
				   bool * value) const
{
  string token = "\"" + key + "\"";
  size_t pos = line.find(token);
  if(pos == string::npos) {
    return false;
  }
  pos = line.find(':', pos + token.size());
  if(pos == string::npos) {
    return false;
  }
  ++pos;
  while((pos < line.size()) && isspace(line[pos])) {
    ++pos;
  }
  if(line.compare(pos, 4, "true") == 0) {
    *value = true;
    return true;
  } else if(line.compare(pos, 5, "false") == 0) {
    *value = false;
    return true;
  }
  return false;
}

bool JsonTraceReader::_ExtractIntArray(string const & line, string const & key,
				       vector<long long> * values) const
{
  values->clear();
  string token = "\"" + key + "\"";
  size_t pos = line.find(token);
  if(pos == string::npos) {
    return false;
  }
  pos = line.find(':', pos + token.size());
  if(pos == string::npos) {
    return false;
  }
  size_t start = line.find('[', pos);
  if(start == string::npos) {
    return false;
  }
  size_t end = line.find(']', start);
  if(end == string::npos) {
    return false;
  }
  string array_str = line.substr(start + 1, end - start - 1);
  stringstream ss(array_str);
  string elem;
  while(getline(ss, elem, ',')) {
    elem = _Trim(elem);
    if(elem.empty()) continue;
    values->push_back(atoll(elem.c_str()));
  }
  return !values->empty();
}

bool JsonTraceReader::_ExtractDestinations(string const & line,
					   vector<int> * dests)
{
  dests->clear();
  long long raw_dest = -1;
  if(_ExtractInt(line, "dest_id", &raw_dest)) {
    dests->push_back(_MapId((int)raw_dest));
    return true;
  }
  vector<long long> list;
  if(_ExtractIntArray(line, "dest_ids", &list)) {
    for(size_t i = 0; i < list.size(); ++i) {
      dests->push_back(_MapId((int)list[i]));
    }
    return true;
  }
  return false;
}

void JsonTraceReader::_FinalizePacket()
{
  if(!_partial_valid) {
    return;
  }
  if(_partial.dests.empty()) {
    cerr << "Trace packet " << _partial.packet_id << " is missing destination information." << endl;
    exit(-1);
  }
  if(_partial.dests.size() > 1 && !_multicast_warning) {
    cout << "JSON trace contains multicast packets. Replicating packet "
	 << _partial.packet_id << " for each destination." << endl;
    _multicast_warning = true;
  }
  for(size_t i = 0; i < _partial.dests.size(); ++i) {
    JsonTracePacket pkt;
    pkt.time = _partial.time;
    pkt.source = _partial.source;
    pkt.dest = _partial.dests[i];
    pkt.size = _partial.size ? _partial.size : 1;
    pkt.cl = _partial.cl;
    pkt.packet_id = _partial.packet_id;
    _queued_packets.push(pkt);
  }
  _partial_valid = false;
}

bool JsonTraceReader::_ParseFlitLine(string const & line)
{
  if(line.empty() || (line[0] != '{')) {
    return false;
  }

  bool head = false;
  bool tail = false;
  if(!_ExtractBool(line, "head", &head)) {
    return false;
  }
  _ExtractBool(line, "tail", &tail);

  long long packet_id = -1;
  if(!_ExtractInt(line, "packet_id", &packet_id)) {
    cerr << "Trace flit missing packet_id field." << endl;
    exit(-1);
  }

  long long flits_in_packet = -1;
  if(!_ExtractInt(line, "flits_in_packet", &flits_in_packet)) {
    cerr << "Trace flit missing flits_in_packet field." << endl;
    exit(-1);
  }

  long long flit_time = 0;
  if(!_ExtractInt(line, "t", &flit_time)) {
    cerr << "Trace flit missing timestamp field 't'." << endl;
    exit(-1);
  }

  long long raw_src = -1;
  if(!_ExtractInt(line, "src_id", &raw_src)) {
    cerr << "Trace flit missing src_id." << endl;
    exit(-1);
  }

  long long class_id = 0;
  _ExtractInt(line, "class_id", &class_id);

  if(head) {
    _partial.packet_id = packet_id;
    _partial.expected_flits = (unsigned int)flits_in_packet;
    _partial.seen_flits = 1;
    _partial.time = _ScaleTime(flit_time);
    _partial.source = _MapId((int)raw_src);
    _partial.cl = (int)class_id;
    _partial.size = (unsigned int)flits_in_packet;
    _partial.dests.clear();
    if(!_ExtractDestinations(line, &_partial.dests)) {
      cerr << "Trace flit missing destination information." << endl;
      exit(-1);
    }
    _partial_valid = true;
  } else {
    if(!_partial_valid || (_partial.packet_id != packet_id)) {
      cerr << "Trace flits are out of order for packet " << packet_id << "." << endl;
      exit(-1);
    }
    ++_partial.seen_flits;
  }

  if(tail) {
    if(!_partial_valid) {
      cerr << "Trace tail encountered without a head for packet "
	   << packet_id << "." << endl;
      exit(-1);
    }
    if(_partial.expected_flits && (_partial.seen_flits != _partial.expected_flits)) {
      cerr << "Trace packet " << packet_id << " expected "
	   << _partial.expected_flits << " flits but saw "
	   << _partial.seen_flits << "." << endl;
      exit(-1);
    }
    _FinalizePacket();
    return true;
  }
  return false;
}

bool JsonTraceReader::NextPacket(JsonTracePacket * packet)
{
  if(!_stream || !_stream->is_open()) {
    cerr << "JSON trace stream is not initialized." << endl;
    exit(-1);
  }
  if(!_queued_packets.empty()) {
    *packet = _queued_packets.front();
    _queued_packets.pop();
    ++_packets_read;
    return true;
  }
  if(limit_reached()) {
    return false;
  }
  string line;
  while(getline(*_stream, line)) {
    if(_ParseFlitLine(line) && !_queued_packets.empty()) {
      *packet = _queued_packets.front();
      _queued_packets.pop();
      ++_packets_read;
      return true;
    }
  }
  return false;
}
