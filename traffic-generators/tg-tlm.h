/*
 * Copyright (c) 2018 Xilinx Inc.
 * Written by Francisco Iglesias.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef TG_TLM_H_
#define TG_TLM_H_

#include <sstream>
#include <vector>

#define SC_INCLUDE_DYNAMIC_PROCESSES

#include "systemc"
using namespace sc_core;
using namespace sc_dt;
using namespace std;

#include "tlm.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"
#include "data-transfer.h"
#include "itraffic-desc.h"
#include <tinyxml2.h>

using namespace tinyxml2;

SC_MODULE(TLMTrafficGenerator)
{
	typedef void (*DoneCallback)(TLMTrafficGenerator *gen, int threadID);

	tlm_utils::simple_initiator_socket<TLMTrafficGenerator> socket;

	SC_HAS_PROCESS(TLMTrafficGenerator);
	TLMTrafficGenerator(sc_core::sc_module_name name, int numThreads = 1) :
		m_debug(false),
		m_startDelay(SC_ZERO_TIME)
	{
		int i;

		for (i = 0; i < numThreads; i++) {
			m_tData.push_back(ThreadData(this, i));
		}

		SC_THREAD(run);
	}

	void enableDebug()
	{
		m_debug = true;
	}

	void addTransfers(ITrafficDesc *transfers,
				int threadId = 0, DoneCallback c = NULL)
	{
		if (threadId < (int) m_tData.size()) {
			m_tData[threadId].SetTransfers(transfers);
			m_tData[threadId].SetCallback(c);
			m_tData[threadId].Proceed();
		}
	}

	void setStartDelay(sc_time startDelay)
	{
		m_startDelay = startDelay;
	}

	template<typename T>
	void addTransfers(T& transfers, int threadId = 0, DoneCallback c = NULL)
	{
		addTransfers(reinterpret_cast<ITrafficDesc*>(&transfers),
								threadId,
								c);
	}

	// 解析数据字符串为字节数组
	std::vector<uint8_t> parseDataString(const std::string& dataStr) {
		std::vector<uint8_t> byteArray;
		std::istringstream iss(dataStr);
		std::string token;
		
		// 移除开头的 '{'
		if (!dataStr.empty() && dataStr[0] == '{') {
			iss.ignore(1);
		}
		
		// 解析每个字节值
		while (std::getline(iss, token, ',')) {
			// 移除空白字符和 '0x' 前缀
			token.erase(0, token.find_first_not_of(" \t\n\r\f\v"));
			token.erase(token.find_last_not_of(" \t\n\r\f\v") + 1);
			
			if (token.size() >= 2 && token[0] == '0' && std::tolower(token[1]) == 'x') {
				token = token.substr(2);
			}
			
			// 转换16进制字符串为字节
			try {
				uint8_t byte = static_cast<uint8_t>(std::stoul(token, nullptr, 16));
				byteArray.push_back(byte);
			} catch (...) {
				std::cerr << "解析数据失败: " << token << std::endl;
			}
		}
		
		return byteArray;
	}

	DataTransferVec get_config(const char* xml_file) {
		XMLDocument doc;
		XMLError eResult = doc.LoadFile(xml_file);
	
		if (eResult != XML_SUCCESS) {
			cerr << "Error loading XML file: " << doc.ErrorName() << endl;
		}
	
		XMLElement* root = doc.FirstChildElement("gen");
		if (root == nullptr) {
			cerr << "Error: Could not find the root element 'gen'." << endl;
		}
		
		XMLElement* cmdElement = root->FirstChildElement("cmd");
		while (cmdElement != nullptr) {
			DataTransfer transfer;
			uint64_t addr = 0;
			const char* data_ptr = nullptr;
			std::vector<char> byte_vector;
			uint64_t length = 0;

			const XMLAttribute* nameAttribute = cmdElement->FindAttribute("name");
	
			if (nameAttribute) {
				string name = nameAttribute->Value();
	
				if (name == "WRITE") {
					cmdElement->QueryAttribute("addr", &addr);
					cmdElement->QueryAttribute("data", &data_ptr);
					std::string dataStr(data_ptr);
        
					// 解析数据
					std::vector<uint8_t> dataBytes = parseDataString(dataStr);
					unsigned char* data = new unsigned char[dataBytes.size()];
					int i = 0;
					for (auto byte : dataBytes) {
						transfer.length++;
						data[i++] = byte;
						//std::cout << "0x" << std::hex << static_cast<int>(byte) << " ";
					}
					//std::cout << std::endl;

					transfer.cmd = DataTransfer::WRITE;
					transfer.addr = addr;
					transfer.data = data;
					transfer.streaming_width = transfer.length;
					m_data_transfers.push_back(transfer);
				} else if (name == "READ") {
					cmdElement->QueryAttribute("addr", &addr);
					cmdElement->QueryAttribute("length", &length);
					transfer.cmd = DataTransfer::READ;
					transfer.addr = addr;
					transfer.length = length;
					transfer.streaming_width = length;
					m_data_transfers.push_back(transfer);
				} else {
					if (m_data_transfers.size() > 0) {
						DataTransfer& last = m_data_transfers.back();
			
						if (name == "EXPECT") {
							cmdElement->QueryAttribute("data", &data_ptr);
							cmdElement->QueryAttribute("length", &length);
							std::string dataStr(data_ptr);
				
							// 解析数据
							std::vector<uint8_t> dataBytes = parseDataString(dataStr);
							unsigned char* data = new unsigned char[dataBytes.size()];
							int i = 0;
							for (auto byte : dataBytes) {
								data[i++] = byte;
							}

							last.expect = data;
						} else if (name == "BYTEENABLE") {
							cmdElement->QueryAttribute("data", &data_ptr);
							cmdElement->QueryAttribute("length", &length);
							std::string dataStr(data_ptr);
				
							// 解析数据
							std::vector<uint8_t> dataBytes = parseDataString(dataStr);
							unsigned char* data = new unsigned char[dataBytes.size()];
							int i = 0;
							for (auto byte : dataBytes) {
								data[i++] = byte;
							}

							last.byte_enable = data;
							last.byte_enable_length = length;
						} else {
							cout << " - Unknown command type." << endl;
						}
					}
				}
			} else {
				cerr << "Error: 'name' attribute not found in a 'cmd' element." << endl;
			}
	
			// 移动到下一个同级的 <cmd> 元素
			cmdElement = cmdElement->NextSiblingElement("cmd");
		}

		return m_data_transfers;
	}

public:
	DataTransferVec m_data_transfers;

private:
	class ThreadData {
	public:
		ThreadData(TLMTrafficGenerator *gen, int id) :
			m_gen(gen),
			m_id(id),
			m_callback(NULL),
			m_transfers(NULL)
		{}

		ThreadData(ThreadData&& d) :
			m_gen(d.m_gen),
			m_id(d.m_id),
			m_callback(NULL),
			m_transfers(NULL)
		{}

		ITrafficDesc *GetTransfers()
		{
			ITrafficDesc *transfers = m_transfers;

			m_transfers = NULL;

			return transfers;
		}

		void SetTransfers(ITrafficDesc *transfers)
		{
			m_transfers = transfers;
		}

		void Proceed()
		{
			if (sc_is_running()) {
				m_proceed.notify();
			}
		}

		sc_event& ProceedEvent() { return m_proceed; }

		bool Done()
		{
			return m_transfers == NULL;
		}

		void RunCallback()
		{
			if (m_callback) {
				m_callback(m_gen, m_id);
			}
		}

		void SetCallback(DoneCallback callback)
		{
			m_callback = callback;
		}

		void SetId(int id) { m_id = id; }
		int GetId() { return m_id; }

		void SetTTG(TLMTrafficGenerator *gen) { m_gen = gen; }

	private:
		TLMTrafficGenerator *m_gen;
		int m_id;
		DoneCallback m_callback;
		ITrafficDesc *m_transfers;
		sc_event m_proceed;
	};


	void run()
	{
		wait(m_startDelay);

		for (auto& td : m_tData) {
			sc_spawn(sc_bind(&TLMTrafficGenerator::process,
					this,
					&td));
		}
	}

	void process(ThreadData *td)
	{
		while (true) {
			ITrafficDesc *transfers = td->GetTransfers();

			if (transfers) {
				generate(transfers);
			}

			td->RunCallback();

			if (td->Done()) {
				wait(td->ProceedEvent());
			}
		}
	}

	void generate(ITrafficDesc *transfers)
	{
		while (!transfers->done()) {
			tlm::tlm_generic_payload trans;
			sc_time delay = sc_time(10, SC_NS);
			uint8_t *data = NULL;

			if (m_debug) {
				cout << std::string(80, '-') << endl;
				cout << "[" << sc_time_stamp() << "]"
					<< endl << endl;
			}

			trans.set_command(transfers->getCmd());
			trans.set_address(transfers->getAddress());

			trans.set_data_length(transfers->getDataLength());

			if (trans.is_write()) {
				trans.set_data_ptr(transfers->getData());
			} else {
				data = new uint8_t[transfers->getDataLength()];
				trans.set_data_ptr(data);
			}

			if (transfers->getByteEnable()) {
				trans.set_byte_enable_ptr(transfers->getByteEnable());
				trans.set_byte_enable_length(transfers->getByteEnableLength());
			} else {
				trans.set_byte_enable_ptr(nullptr);
				trans.set_byte_enable_length(0);
			}

			trans.set_streaming_width(transfers->getStreamingWidth());

			trans.set_dmi_allowed(false);
			trans.set_response_status( tlm::TLM_INCOMPLETE_RESPONSE );

			transfers->setExtensions(&trans);

			if (m_debug) {
				debugWrite(&trans);
			}

			socket->b_transport(trans, delay);

			if ( trans.is_response_error() ) {
				// Print response string
				char txt[100];
				sprintf(txt, "Error from b_transport, response status = %s",
				trans.get_response_string().c_str());
				SC_REPORT_ERROR("TrafficGenerator", txt);
			}

			if (m_debug) {
				debugRead(&trans, transfers->getExpect());
			}

			if (transfers->getExpect() &&
				memcmp(transfers->getExpect(),
					trans.get_data_ptr(), trans.get_data_length())) {
				SC_REPORT_ERROR("TLMTrafficGenerator",
						"Read data not same as expected!\n");
			}

			delete[] data;
			transfers->next();
		}
	}

	void debugWrite(tlm::tlm_generic_payload *trans)
	{
		if (trans->is_write()) {
			cout << "Write : 0x"
				<< hex << trans->get_address()
				<< ", length = " << dec << trans->get_data_length()
				<< ", streaming_width = " << dec << trans->get_streaming_width()
				<< "\n\ndata = { ";
			for (uint32_t i = 0; i < trans->get_data_length(); i++) {
				cout << "0x" << std::hex
					<< static_cast<unsigned int>(trans->get_data_ptr()[i])
					<< ", ";
			}
			cout << "}\n";
			if (trans->get_byte_enable_length()) {
				cout << "byte_enable[" << trans->get_byte_enable_length() << "] = {";
				for (uint32_t i = 0; i < trans->get_byte_enable_length(); i++) {
					cout << "0x" << std::hex
						<< static_cast<unsigned int>(trans->get_byte_enable_ptr()[i])
						<< ", ";
				}
				cout << " }\n";
			}
		}
	}

	void debugRead(tlm::tlm_generic_payload *trans, unsigned char *expect)
	{
		if (trans->is_read()) {
			cout << "Read: 0x"
				<< hex << trans->get_address()
				<< ", length = " << dec << trans->get_data_length()
				<< ", streaming_width = " << dec << trans->get_streaming_width()
				<< "\n\nReceived: { ";
			for (uint32_t i = 0; i < trans->get_data_length(); i++) {
				cout << "0x" << std::hex
					<< static_cast<unsigned int>(trans->get_data_ptr()[i])
					<< ", ";
			}
			cout << " }\n";
			if (trans->get_byte_enable_length()) {
				cout << "byte_enable[" << trans->get_byte_enable_length() << "] = {";
				for (uint32_t i = 0; i < trans->get_byte_enable_length(); i++) {
					cout << "0x" << std::hex
						<< static_cast<unsigned int>(trans->get_byte_enable_ptr()[i])
						<< ", ";
				}
				cout << " }\n";
			}
			if (expect) {
				cout << "Expected: { ";
				for (uint32_t i = 0; i < trans->get_data_length(); i++) {
					cout << "0x" << hex
						<< static_cast<unsigned int>(expect[i])
						<< ", ";
				}
				cout << " }\n";
			}
		}
	}

	enum { SZ_32K = 32 * 1024 };

	std::vector<ThreadData> m_tData;
	bool m_debug;
	sc_time m_startDelay;
};

#endif /* TG_TLM_H_ */
