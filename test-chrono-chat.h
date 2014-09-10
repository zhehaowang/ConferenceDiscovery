/**
 * Copyright (C) 2013-2014 Regents of the University of California.
 * @author: Jeff Thompson <jefft0@remap.ucla.edu>, 
 * Incorporated directly into ndnrtc by Zhehao Wang. <wangzhehao410305@gmail.com>
 * Derived from ChronoChat-js by Qiuhan Ding and Wentao Shang.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version, with the additional exemption that
 * compiling, linking, and/or using OpenSSL is allowed.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * A copy of the GNU General Public License is in the file COPYING.
 */

// Only compile if ndn-cpp-config.h defines NDN_CPP_HAVE_PROTOBUF = 1.
#ifndef __ndnrtc__addon__chrono__chat__
#define __ndnrtc__addon__chrono__chat__

#include <ndn-cpp/ndn-cpp-config.h>
#if NDN_CPP_HAVE_PROTOBUF

#include <cstdlib>
#include <string>
#include <iostream>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <math.h>
#include <sstream>
#include <stdexcept>
#include <openssl/rand.h>

#include <ndn-cpp/sync/chrono-sync2013.hpp>
#include <ndn-cpp/security/identity/memory-identity-storage.hpp>
#include <ndn-cpp/security/identity/memory-private-key-storage.hpp>
#include <ndn-cpp/security/policy/no-verify-policy-manager.hpp>
#include <ndn-cpp/transport/tcp-transport.hpp>

#include "chatbuf.pb.h"
#include "external-observer.h"

#if NDN_CPP_HAVE_TIME_H
#include <time.h>
#endif
#if NDN_CPP_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

using namespace std;
using namespace ndn;
using namespace ndn::func_lib;

namespace chrono_chat
{
  class Chat
  {
  public:
	Chat
	  (const Name& broadcastPrefix,
	   const std::string& screenName, const std::string& chatRoom,
	   const Name& hubPrefix, ExternalObserver *observer, Face& face, KeyChain& keyChain,
	   Name certificateName)
	  : screen_name_(screenName), chatroom_(chatRoom), maxmsgcachelength_(100),
		isRecoverySyncState_(true), sync_lifetime_(5000.0), observer_(observer),
		faceProcessor_(face), keyChain_(keyChain), certificateName_(certificateName),
		broadcastPrefix_(broadcastPrefix)
	{
	  chat_prefix_ = Name(hubPrefix).append(chatroom_).append(Chat::getRandomString());
	  int session = (int)::round(getNowMilliseconds()  / 1000.0);
	  ostringstream tempStream;
	  tempStream << screen_name_ << session;
	  usrname_ = tempStream.str();
	  
	  sync_.reset(new ChronoSync2013
		(bind(&Chat::sendInterest, this, _1, _2),
		 bind(&Chat::initial, this), chat_prefix_,
		 broadcastPrefix_.append(chatroom_), session,
		 faceProcessor_, keyChain_, 
		 certificateName_, sync_lifetime_, onRegisterFailed));
	  
	  faceProcessor_.registerPrefix
		(chat_prefix_, bind(&Chat::onInterest, this, _1, _2, _3, _4),
		 onRegisterFailed);   
	}
	
	~Chat()
	{
	  
	}
	
	// Send a chat message.
	void
	sendMessage(const std::string& chatmsg);

	// Send leave message and leave.
	void
	leave();

	/**
	 * Use gettimeofday to return the current time in milliseconds.
	 * @return The current time in milliseconds since 1/1/1970, including fractions
	 * of a millisecond according to timeval.tv_usec.
	 */
	static MillisecondsSince1970
	getNowMilliseconds();

	/*** Methods for printing chat messages ***/
	
	int notifyObserverWithChat(const char *format, ...) const
	{
	  va_list args;
  
	  static char msg[256];
  
	  va_start(args, format);
	  vsprintf(msg, format, args);
	  va_end(args);
  
	  if (observer_)
		observer_->onStateChanged("chat - ", msg);
	  else {
		cout << "chat - " << msg << endl;
	  }
	  return 1;
	}

  private:
	// Initialization: push the JOIN message in to the msgcache, update roster and start heartbeat.
	void
	initial();

	// Send a Chat Interest to fetch chat messages after get the user gets the Sync data packet back but will not send interest.
	void
	sendInterest
	  (const std::vector<ChronoSync2013::SyncState>& syncStates, bool isRecovery);

	// Send back Chat Data Packet which contains the user's message.
	void
	onInterest
	  (const ptr_lib::shared_ptr<const Name>& prefix,
	   const ptr_lib::shared_ptr<const Interest>& inst, Transport& transport,
	   uint64_t registeredPrefixId);

	// Processing the incoming Chat data.
	void
	onData
	  (const ptr_lib::shared_ptr<const Interest>& inst,
	   const ptr_lib::shared_ptr<Data>& co);

	void
	chatTimeout(const ptr_lib::shared_ptr<const Interest>& interest);

	/**
	 * This repeatedly calls itself after a timeout to send a heartbeat message
	 * (chat message type HELLO).
	 * This method has an "interest" argument because we use it as the onTimeout
	 * for Face.expressInterest.
	 */
	void
	heartbeat(const ptr_lib::shared_ptr<const Interest> &interest);

	/**
	 * This is called after a timeout to check if the user with prefix has a newer
	 * sequence number than the given temp_seq. If not, assume the user is idle and
	 * remove from the roster and print a leave message.
	 * This method has an "interest" argument because we use it as the onTimeout
	 * for Face.expressInterest.
	 */
	void
	alive
	  (const ptr_lib::shared_ptr<const Interest> &interest, int temp_seq,
	   const std::string& name, int session, const std::string& prefix);

	/**
	 * Append a new CachedMessage to msgcache, using given messageType and message,
	 * the sequence number from sync_->getSequenceNo() and the current time. Also
	 * remove elements from the front of the cache as needed to keep
	 * the size to maxmsgcachelength_.
	 */
	void
	messageCacheAppend(int messageType, const std::string& message);

	// Generate a random name for ChronoSync.
	static std::string
	getRandomString();

	static void
	onRegisterFailed(const ptr_lib::shared_ptr<const Name>& prefix);

	/**
	 * This is a do-nothing onData for using expressInterest for timeouts.
	 * This should never be called.
	 */
	static void
	dummyOnData
	  (const ptr_lib::shared_ptr<const Interest>& interest,
	   const ptr_lib::shared_ptr<Data>& data);
	
	class CachedMessage {
	public:
	  CachedMessage
		(int seqno, int msgtype, const std::string& msg, MillisecondsSince1970 time)
	  : seqno_(seqno), msgtype_(msgtype), msg_(msg), time_(time)
	  {}

	  int
	  getSequenceNo() const { return seqno_; }

	  int
	  getMessageType() const { return msgtype_; }

	  const std::string&
	  getMessage() const { return msg_; }

	  MillisecondsSince1970
	  getTime() const { return time_; }

	private:
	  int seqno_;
	  // This is really enum SyncDemo::ChatMessage_ChatMessageType, but make it
	  //   in int so that the head doesn't need to include the protobuf header.
	  int msgtype_;
	  std::string msg_;
	  MillisecondsSince1970 time_;
	};
	
	std::vector<ptr_lib::shared_ptr<CachedMessage> > msgcache_;
	std::vector<std::string> roster_;
	size_t maxmsgcachelength_;
	bool isRecoverySyncState_;
	std::string screen_name_;
	std::string chatroom_;
	std::string usrname_;
	
	Name broadcastPrefix_;
	Name chat_prefix_;
	
	Milliseconds sync_lifetime_;
	ptr_lib::shared_ptr<ChronoSync2013> sync_;
	
	Face& faceProcessor_;
	KeyChain& keyChain_;
	Name certificateName_;
	
	ExternalObserver *observer_;
  };
}



#else // NDN_CPP_HAVE_PROTOBUF

#include <iostream>

using namespace std;

int main(int argc, char** argv)
{
  cout <<
    "This program uses Protobuf but it is not installed. Install it and ./configure again." << endl;
}

#endif // NDN_CPP_HAVE_PROTOBUF

#endif // __ndnrtc__chrono__chat__