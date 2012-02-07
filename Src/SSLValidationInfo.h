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

#ifndef SSLVALIDATIONINFO_H
#define SSLVALIDATIONINFO_H

#include <string>

class SSLValidationInfo {

public:

        enum {
                ValidationFailReason_SSL,
                ValidationFailReason_NameMismatch
        };

        enum {
                AcceptDecision_Reject = 0,
                AcceptDecision_AcceptPermanently = 1,
                AcceptDecision_AcceptForSessionOnly = 2
        };

        SSLValidationInfo(const std::string& certFileAndPath, const std::string& userMsgString,const std::string& commonName,
                                                        const std::string& hostName, const std::string& signingCA, int failReason) :
                m_certFileAndPath(certFileAndPath),
                m_userMsgString(userMsgString),
                m_CommonName(commonName),
                m_HostName(hostName),
                m_signingCA(signingCA),
                m_validationFailReason(failReason) {}

        std::string getCertFile() { return m_certFileAndPath; }
        std::string getUserMessage() { return m_userMsgString; }
        std::string getCommonName() { return m_CommonName; }
        std::string getHostName() { return m_HostName; }
        std::string getSigningCA() { return m_signingCA; }
        int			getValidationFailureReasonCode() { return m_validationFailReason; }
        int			getAcceptDecision() { return m_acceptDecision; }
        void		setAcceptDecision(int decision) { m_acceptDecision = decision;}

private:

        std::string m_certFileAndPath;
        std::string m_userMsgString;
        std::string m_CommonName;
        std::string m_HostName;
        std::string m_signingCA;
        int			m_validationFailReason;

        int 		m_acceptDecision;
};

#endif // SSLVALIDATIONINFO_H
