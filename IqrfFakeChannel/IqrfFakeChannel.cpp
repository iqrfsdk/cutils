#include "IqrfFakeChannel.h"
#include "IqrfLogging.h"
#include "CdcInterface.h"
#include <array>
#include <algorithm>
#include <stdexcept>

static ModuleInfo moduleInfo = {{0,1,2,3}, 0x38, 4, {0,1}};

IqrfFakeChannel::IqrfFakeChannel(const std::string& fileName)
{
    file.open(fileName);
    print("IQRF Fake channel for testing libtr\n");
    generateDownloadData();
}

IqrfFakeChannel::~IqrfFakeChannel()
{
}

void IqrfFakeChannel::sendTo(const std::basic_string<unsigned char>& message)
{
    print("Send to TR: ", message);
}

void IqrfFakeChannel::enterProgrammingMode() {
    print("Enter TR Programming Mode\n");
}

void IqrfFakeChannel::terminateProgrammingMode() {
    print("Terminate TR Programming Mode\n");
}

void IqrfFakeChannel::upload(unsigned char target, const std::basic_string<unsigned char>& message) {
    print("Upload to TR: ", target, message);
    putUploadData(target, message);
}

void IqrfFakeChannel::download(unsigned char target, const std::basic_string<unsigned char>& message, std::basic_string<unsigned char>& data) {
    getDownloadData(target, message, data);
    print("Download from TR: ", target, message, data);
}

void IqrfFakeChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
    print("Register Receive From Handler\n");
}

void IqrfFakeChannel::unregisterReceiveFromHandler()
{
    print("Unegister Receive From Handler\n");
}

void IqrfFakeChannel::print(std::string comment, unsigned char target, const std::basic_string<unsigned char>& message, const std::basic_string<unsigned char>& data) {
    std::basic_string<unsigned char>::const_iterator it;
    print(comment, target, message);
    file << "  Response: ";
    for (it = data.begin(); it != data.end(); it++) {
        file << std::setfill ('0') << std::setw(2) << std::hex << static_cast<int>(*it) << " ";
    }
    file << std::endl;
    
}

void IqrfFakeChannel::print(std::string comment, unsigned char target, const std::basic_string<unsigned char>& data){
    std::basic_string<unsigned char>::const_iterator it;
    print(comment);
    file << "  Target:  " << std::setw(2) << std::setfill ('0') << std::hex << static_cast<int>(target) << "\n";
    file << "  Command: ";
    for (it = data.begin(); it != data.end(); it++) {
        file << std::setw(2) << std::setfill ('0') << std::hex << static_cast<int>(*it) << " ";
    }
    file << std::endl;
}

void IqrfFakeChannel::print(std::string comment, const std::basic_string<unsigned char>& data){
    std::basic_string<unsigned char>::const_iterator it;
    print(comment);
    
    file << "  Command: ";
    for (it = data.begin(); it != data.end(); it++) {
        file << std::setw(2) << std::setfill ('0') << std::hex << static_cast<int>(*it) << " ";
    }
    file << std::endl;
}

void IqrfFakeChannel::print(std::string comment){
    file << comment << std::endl;
}


void IqrfFakeChannel::generateDownloadData(void) {
    model.config[0] = 0x5f;
    for (size_t i = 1; i < model.config.size(); i++) {
        model.config[i] = i;
        model.config[0] ^= i;
    }
    model.rfpmg = 0xba;
    model.rfband = 0x01;
    for (size_t i = 0; i < model.accessPwd.size(); i++) {
        model.accessPwd[i] = 10 + i;
    }
    for (size_t i = 0; i < model.userKey.size(); i++) {
        model.userKey[i] = 30 + i;
    }
    for (size_t i = 0; i < model.flash.size() / 8; i += 8) {
        model.flash[i*8 + 0] = 0xde;
        model.flash[i*8 + 1] = 0xad;
        model.flash[i*8 + 2] = 0xbe;
        model.flash[i*8 + 3] = 0xef;
        model.flash[i*8 + 4] = 0xde;
        model.flash[i*8 + 5] = 0xad;
        model.flash[i*8 + 6] = 0xba;
        model.flash[i*8 + 7] = 0xbe;
    }
    
    for (size_t i = 0x3A00; i < 0x3FFF; i++) {
        model.flash[i] = ((i -  0x3A00) + 50) % 256;
    }
    
    for (size_t i = 0x2C00; i < 0x37BF; i++) {
        model.flash[i] = ((i -  0x2C00) + 70) % 256;
    }
    
    for (size_t i = 0; i < model.internalEeprom.size(); i++) {
        model.internalEeprom[i] = (i + 90) % 256;
    }
    for (size_t i = 0; i < model.externalEeprom.size(); i++) {
        model.externalEeprom[i] = (i + 110) % 256;
    }
    for (size_t i = 0; i < model.special.size(); i++) {
        model.special[i] = (i + 130);
    }
}

void IqrfFakeChannel::getDownloadData(unsigned char target, const std::basic_string<unsigned char>& message,  std::basic_string<unsigned char>& data){
    size_t addr = 0;
    switch (target) {
        case 0: 
            data.resize(32, 0);
            std::copy_n(model.config.begin(), 32, data.begin());
            break;
        case 1:
            data.resize(1, 0);
            data[0] = model.rfpmg;
            break;
        case 2:
            data.resize(1, 0);
            data[0] = model.rfband;
            break;
        case 5:
            addr = (static_cast<size_t>(message[1]) << 8) + static_cast<size_t>(message[0]);
            if (!(((addr >= 0x3A00 && addr < 0x3FFF) || (addr >= 0x2C00 && addr < 0x37BF)) && addr % 32 == 0)) {
                throw std::runtime_error("Invalid download addr - " + std::to_string(addr) + "!");
            }
            data.resize(32, 0);
            for (int i = 0; i < 32; i++) {
                data[i] = model.flash[addr + 2 * i] ^ model.flash[addr + 2 * i + 1];
            }
            break;
        case 6:
            addr = (static_cast<size_t>(message[1]) << 8) + static_cast<size_t>(message[0]);
            if (!((addr >= 0x0 && addr < 0x00a0) && addr % 32 == 0)) {
                throw std::runtime_error("Invalid download addr - " + std::to_string(addr) + "!");
            }
            data.resize(32, 0);
            std::copy_n(model.internalEeprom.begin() + addr, 32, data.begin());
            break;
        case 7:
            addr = (static_cast<size_t>(message[1]) << 8) + static_cast<size_t>(message[0]);
            if (!((addr >= 0x0 && addr < 0x7fe0) && addr % 32 == 0)) {
                throw std::runtime_error("Invalid download addr - " + std::to_string(addr) + "!");
            }
            data.resize(32, 0);
            std::copy_n(model.externalEeprom.begin() + addr, 32, data.begin());
            break;
        default:
            throw std::runtime_error("Invalid download target - " + std::to_string(target) +" !");
            break;        
    }
}

void IqrfFakeChannel::putUploadData(unsigned char target, const std::basic_string<unsigned char>& message) {
    size_t addr = 0;
    switch (target) {
        case 128: 
            if (message.length() != 32)
                throw std::runtime_error("Invalid message length!");
            std::copy_n(message.begin(), 32, model.config.begin());
            break;
        case 129:
            if (message.length() != 1)
                throw std::runtime_error("Invalid message length!");
            model.rfpmg = message[0];
            break;
        case 130:
            if (message.length() != 1)
                throw std::runtime_error("Invalid message length!");
            model.rfband = message[0];
            break;
        case 131:
            if (message.length() != 16)
                throw std::runtime_error("Invalid message length!");
            std::copy_n(message.begin(), 16, model.accessPwd.begin());
            break;
        case 132:
            if (message.length() != 16)
                throw std::runtime_error("Invalid message length!");
            std::copy_n(message.begin(), 16, model.userKey.begin());
            break;
        case 133:
            if (message.length() != 34)
                throw std::runtime_error("Invalid message length!");
            addr = (static_cast<size_t>(message[1]) << 8) + static_cast<size_t>(message[0]);
            if (!(((addr >= 0x3A00 && addr < 0x3FFF) || (addr >= 0x2C00 && addr < 0x37BF)) && addr % 16 == 0)) {
                throw std::runtime_error("Invalid download addr - " + std::to_string(addr) + "!");
            }
            std::copy_n(message.begin() + 2, 32, model.flash.begin() + addr);
            break;
        case 134:
            if (message.length() < 3 || message.length() > 34)
                throw std::runtime_error("Invalid message length!");
            addr = (static_cast<size_t>(message[1]) << 8) + static_cast<size_t>(message[0]);
            if (!((addr >= 0x0 && addr < 0x00a0) && addr % 32 == 0)) {
                throw std::runtime_error("Invalid download addr - " + std::to_string(addr) + "!");
            }
            std::copy_n(message.begin() + 2, message.length() - 2, model.internalEeprom.begin() + addr);
            break;
        case 135:
            if (message.length() != 34)
                throw std::runtime_error("Invalid message length!");
            addr = (static_cast<size_t>(message[1]) << 8) + static_cast<size_t>(message[0]);
            if (!((addr >= 0x0 && addr < 0x3fe0) && addr % 32 == 0)) {
                throw std::runtime_error("Invalid upload addr - " + std::to_string(addr) + "!");
            }
            std::copy_n(message.begin() + 2, 32, model.externalEeprom.begin() + addr);
            break;
        case 136:
            if (message.length() != 18)
                throw std::runtime_error("Invalid message length!");
            std::copy_n(message.begin(), 18, model.special.begin());
            break;
        default:
            throw std::runtime_error("Invalid download target - " + std::to_string(target) + " !");
            break;
    }
}

void* IqrfFakeChannel::getTRModuleInfo() {
    return &moduleInfo;
}
