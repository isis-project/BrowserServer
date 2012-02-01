/* @@@LICENSE
*
*      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
LICENSE@@@ */

#ifndef __CmdResourceHandlers_h__
#define __CmdResourceHandlers_h__

#include <string>

class CommandHandler
{
	public:
		CommandHandler( const std::string& cmd, const std::string& appId ) 
				: m_command(cmd), m_appId(appId) { }
		~CommandHandler() { }
		
		const std::string command() const { return m_command; }
		const std::string appId() const { return m_appId; }

	private:
		std::string m_command;
		std::string m_appId;
		
		CommandHandler();
};

class RedirectHandler
{
	public:
		RedirectHandler(const std::string& scheme, const std::string& host, const std::string& appId )
			: m_scheme(scheme) , m_host(host) , m_appId(appId) { }
		virtual ~RedirectHandler() {}
		
		const std::string scheme() const { return m_scheme; }
		const std::string host() const { return m_host; }
		const std::string appId() const { return m_appId; }
			
	private:
		
		std::string m_scheme;
		std::string m_host;
		std::string m_appId;
		
		RedirectHandler();
};

class ResourceHandler
{
	public:
		ResourceHandler( const std::string& ext, 
				const std::string& contentType, 
				const std::string& appId, 
				bool stream=false ) 
				: m_fileExt(ext)
				, m_contentType(contentType)
				, m_appId(appId)
				, m_stream(stream) { }
		~ResourceHandler() { }
		
		const std::string appId() const { return m_appId; }
		const std::string fileExt() const { return m_fileExt; }
		const std::string contentType() const { return m_contentType; }
		bool stream() const { return m_stream; }

	private:
		std::string m_fileExt;
		std::string m_contentType;
		std::string m_appId;
		bool m_stream;
		
		ResourceHandler();
};


#endif

