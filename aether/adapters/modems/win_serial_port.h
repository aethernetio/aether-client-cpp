/*
 * Copyright 2025 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AETHER_ADAPTERS_MODEMS_WIN_SERIAL_PORT_H_
#define AETHER_ADAPTERS_MODEMS_WIN_SERIAL_PORT_H_

#include <Windows.h>
#include <vector>
#include <optional>
#include <stdexcept>
#include <string>
#include <memory>

#include "aether/adapters/modems/i_serial_port.h"

namespace ae {

class WINSerialPort : public ISerialPort{
public:
    WINSerialPort(const std::string& portName, DWORD baudRate = CBR_9600);
    ~WINSerialPort();
    void Write(const DataBuffer& data) override;
    std::optional<DataBuffer> Read() override;
private:
    HANDLE hPort_;
    
    void Open(const std::string& portName, DWORD baudRate);
    void ConfigurePort(DWORD baudRate);
    void SetupTimeouts();
    void Close();
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_WIN_SERIAL_PORT_H_