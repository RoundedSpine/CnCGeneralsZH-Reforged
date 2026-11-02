#!/usr/bin/env python3
"""Exercise the production disconnect-ping selection with returned-by-value lists."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
SOURCE = CODE / 'GameEngine/Source/GameNetwork/DisconnectManager.cpp'

class DisconnectPing(unittest.TestCase):
    def test_lifetime_empty_config_and_rotation(self):
        source = SOURCE.read_text()
        selection = source.split('// Send the pings\n', 1)[1].split('\n\t\t\t}', 1)[0]
        program = r'''
#include <cassert>
#include <list>
#include <string>
#include <vector>
struct AsciiString { std::string text; const char *str() const { return text.c_str(); } };
struct Config {
    std::list<AsciiString> servers;
    unsigned queries = 0;
    std::list<AsciiString> getPingServers() { ++queries; return servers; }
};
struct PingRequest { std::string hostname; int repetitions = 0, timeout = 0; };
struct Pinger { std::vector<PingRequest> requests; void addRequest(const PingRequest &r) { requests.push_back(r); } };
#define DEBUG_LOG(args) ((void)0)
void selectPing(Config *TheGameSpyConfig, Pinger *ThePinger, int &m_pingsSent) {
''' + selection + r'''
}
int main() {
    Config config; Pinger pinger; int sent = 0;
    selectPing(&config, nullptr, sent); assert(config.queries == 0);
    selectPing(nullptr, &pinger, sent); assert(pinger.requests.empty());
    selectPing(&config, &pinger, sent); assert(pinger.requests.empty() && sent == 0);
    config.servers = {{"only.example"}};
    for (int i=0; i<3; ++i) selectPing(&config, &pinger, sent);
    assert(pinger.requests.size() == 3 && sent == 5);
    for (const auto &r:pinger.requests) assert(r.hostname == "only.example" && r.repetitions == 5 && r.timeout == 2000);
    config.servers = {{"a.example"},{"b.example"},{"c.example"}};
    selectPing(&config, &pinger, sent); selectPing(&config, &pinger, sent); selectPing(&config, &pinger, sent);
    assert(pinger.requests[3].hostname == "b.example");
    assert(pinger.requests[4].hostname == "c.example");
    assert(pinger.requests[5].hostname == "a.example");
    config.servers = {{"changed.example"}};
    selectPing(&config, &pinger, sent); assert(pinger.requests.back().hostname == "changed.example");
    config.servers.clear(); sent = 0;
    selectPing(&config, &pinger, sent); assert(pinger.requests.size() == 7 && sent == 0);
    assert(config.queries == 9); // Exactly one returned list per usable configuration.
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, exe = Path(folder)/'ping.cpp', Path(folder)/'ping'
            cpp.write_text(program)
            subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-g', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == '__main__': unittest.main()
