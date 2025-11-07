// $Id$

/*
 Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _JSONTRACE_LOADER_HPP_
#define _JSONTRACE_LOADER_HPP_

#include <fstream>
#include <string>
#include <map>
#include <queue>
#include <vector>

using namespace std;

struct JsonTracePacket {
  unsigned long long time;
  int source;
  int dest;
  unsigned int size;
  int cl;
  long long packet_id;
};

class JsonTraceReader {
public:
  JsonTraceReader(const string & trace_path,
		  const string & map_path,
		  long long limit,
		  unsigned int scale,
		  int nodes);
  ~JsonTraceReader();
  bool NextPacket(JsonTracePacket * packet);
  void Reset();
  unsigned long long packets_read() const { return _packets_read; }
  bool limit_reached() const;

private:
  struct PartialPacket {
    long long packet_id;
    unsigned int expected_flits;
    unsigned int seen_flits;
    unsigned long long time;
    int source;
    vector<int> dests;
    int cl;
    unsigned int size;
  };

  string _trace_path;
  string _map_path;
  long long _limit;
  unsigned int _scale;
  int _nodes;
  ifstream * _stream;
  map<int, int> _id_map;
  bool _has_map;
  bool _header_parsed;
  bool _partial_valid;
  bool _multicast_warning;
  PartialPacket _partial;
  unsigned long long _packets_read;
  queue<JsonTracePacket> _queued_packets;

  void _OpenStream();
  void _CloseStream();
  void _LoadMap();
  int _MapId(int original) const;
  bool _ParseFlitLine(string const & line);
  bool _ExtractInt(string const & line, string const & key, long long * value) const;
  bool _ExtractIntArray(string const & line, string const & key, vector<long long> * values) const;
  bool _ExtractBool(string const & line, string const & key, bool * value) const;
  bool _ExtractDestinations(string const & line, vector<int> * dests);
  void _FinalizePacket();
  unsigned long long _ScaleTime(long long raw) const;
};

#endif
