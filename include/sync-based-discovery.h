// ConferenceDiscovery works with SyncBasedDiscovery, handling traffic under user prefix, 
// Similar with the  relationship between chrono-sync and chrono-chat

#ifndef __ndnrtc__addon__object__discovery__
#define __ndnrtc__addon__object__discovery__

#include <ndn-cpp/ndn-cpp-config.h>

#include <ndn-cpp/util/memory-content-cache.hpp>

#include <ndn-cpp/security/identity/memory-identity-storage.hpp>
#include <ndn-cpp/security/identity/memory-private-key-storage.hpp>
#include <ndn-cpp/security/policy/no-verify-policy-manager.hpp>
#include <ndn-cpp/transport/tcp-transport.hpp>

#include <boost/algorithm/string.hpp>
#include <sys/time.h>
#include <iostream>

#include "external-observer.h"

using namespace std;
using namespace ndn;
using namespace ndn::func_lib;
#if NDN_CPP_HAVE_STD_FUNCTION && NDN_CPP_WITH_STD_FUNCTION
// In the std library, the placeholders are in a different namespace than boost.
using namespace func_lib::placeholders;
#endif

namespace conference_discovery
{
  typedef func_lib::function<void
	(const std::vector<std::string>& syncData)>
	  OnReceivedSyncData;

  static MillisecondsSince1970 
  ndn_getNowMilliseconds()
  {
    struct timeval t;
    // Note: configure.ac requires gettimeofday.
    gettimeofday(&t, 0);
    return t.tv_sec * 1000.0 + t.tv_usec / 1000.0;
  }
  
  /**
   * Class interface for the discover object
   */
  class DiscoveryObject
  {
  public:
    
  private:
    
  };
  
  /**
   * This class, upon instantiation, will register a broadcast prefix and 
   * handle all that's going on in the broadcast namespace,
   * Namely periodical broadcast of sync digest, maintenance of outstanding interest, etc
   *
   * Its idea and implementations are pretty similar with ChronoSync, but does not have
   * a dependency upon a not-shrinking digest tree (whose nodes are names and sequence 
   * numbers) and log.
   */
  class SyncBasedDiscovery
  {
  public:
  
    // Double check: does "const int& something" make sense?
    // Does initiation part in constructor implementation do a copy, or merely a reference?
    // Question: does this initiation part call the constructor, or do merely an equal: 
    // seems to be the former, which means, it copies, unless the variable being initialized is a reference member
    // Double check: the thing with cpp initialization sequence in constructor
    
    /**
     * Constructor.
     * @param broadcastPrefix The name prefix for broadcast. Hashes of discovered 
     * and published objects will be appended directly after broadcast prefix
     * @param onReceivedSyncData The callback for the action after receiving sync data.
     * @param face The broadcast face.
     * @param keyChain The keychain to sign things with.
     * @param certificateName The certificate name for locating the certificate.
     */
    SyncBasedDiscovery
      (Name broadcastPrefix, const OnReceivedSyncData& onReceivedSyncData, 
       Face& face, KeyChain& keyChain, Name certificateName)
     : broadcastPrefix_(broadcastPrefix), onReceivedSyncData_(onReceivedSyncData), 
       face_(face), keyChain_(keyChain), certificateName_(certificateName), 
       contentCache_(&face), newComerDigest_("00"), currentDigest_(newComerDigest_),
       defaultDataFreshnessPeriod_(4000), defaultInterestLifetime_(2000), 
       defaultInterval_(500)
    {
      // Storing it in contentCache, the idea is that a set of strings maps to a digest
      contentCache_.registerPrefix
        (broadcastPrefix, bind(&SyncBasedDiscovery::onRegisterFailed, this, _1), 
         bind(&SyncBasedDiscovery::onInterest, this, _1, _2, _3, _4));
      
      Interest interest(broadcastPrefix);
      interest.getName().append(newComerDigest_);
      interest.setInterestLifetimeMilliseconds(defaultInterestLifetime_);
      // setAnswerOriginKind is deprecated
      //interest.setAnswerOriginKind(ndn_Interest_ANSWER_NO_CONTENT_STORE);
  
      face_.expressInterest
        (interest, bind(&SyncBasedDiscovery::onData, this, _1, _2),
         bind(&SyncBasedDiscovery::onTimeout, this, _1));
    };
    
    ~SyncBasedDiscovery()
    {
      
    };
    
    /**
     * onData sorts both the object(string) array received, 
     * and the string array belonging to this object.
     * A lock for objects_ member?
     */
    void onData
      (const ptr_lib::shared_ptr<const Interest>& interest,
       const ptr_lib::shared_ptr<Data>& data);
      
    void onTimeout
      (const ptr_lib::shared_ptr<const Interest>& interest);
      
    void dummyOnData
      (const ptr_lib::shared_ptr<const Interest>& interest,
       const ptr_lib::shared_ptr<Data>& data);
       
    void expressBroadcastInterest
      (const ptr_lib::shared_ptr<const Interest>& interest);
    
    void onInterest
      (const ptr_lib::shared_ptr<const Name>& prefix,
       const ptr_lib::shared_ptr<const Interest>& interest, Transport& transport,
       uint64_t registerPrefixId);
    
    void onRegisterFailed
      (const ptr_lib::shared_ptr<const Name>& prefix);
    
    /**
     * contentCacheAdd copied from ChronoSync2013 implementation
     * Double check its logic
     *
	 * Add the data packet to the contentCache_. Remove timed-out entries
	 * from pendingInterestTable_. If the data packet satisfies any pending
	 * interest, then send the data packet to the pending interest's transport
	 * and remove from the pendingInterestTable_.
	 * @param data
	 */
    void contentCacheAdd(const Data& data);
    
    /**
     * Called when startPublishing, note that this function itself does 
     * add the published name to objects_. The caller should not call addObject to do it agin.
     * @ {string} name: the full (prefix+conferenceName).toUri()
     */
    void publishObject(std::string name);
    
    /**
     * Called when stopConferencePublishing
     */
    void stopPublishingObject(std::string name);
    
    /**
     * Updates the currentDigest_ according to the list of objects
     */
    void stringHash();
    void recomputeDigest();
    
	const std::string newComerDigest_;
	const Milliseconds defaultDataFreshnessPeriod_;
	const Milliseconds defaultInterestLifetime_;
	const Milliseconds defaultInterval_;
    
    /**
     * These functions should be replaced, once we replace objects with something more
     * generic
     */
    std::vector<std::string> getObjects() { return objects_; };
    
    // addObject does not necessarily call updateHash
    int addObject(std::string object, bool updateDigest) {
      std::vector<std::string>::iterator item = std::find(objects_.begin(), objects_.end(), object);
      if (item == objects_.end() && object != "") {
        objects_.push_back(object);
        // Using default comparison '<' here
        std::sort(objects_.begin(), objects_.end()); 
        // Update the currentDigest_ 
        if (updateDigest) {
          recomputeDigest();
        }
        return 1;
      }
      else {
        // The to be added string exists or is empty
        return 0;
      }
    };
    
    // removeObject does not necessarily call updateHash
    int removeObject(std::string object, bool updateDigest) {
      std::vector<std::string>::iterator item = std::find(objects_.begin(), objects_.end(), object);
      if (item != objects_.end()) {
        // I should not need to sort the thing again, if it's just erase. Remains to be tested
        objects_.erase(item);
        // Update the currentDigest_
        if (updateDigest) {
          recomputeDigest();
        }
        return 1;
      }
      else {
        return 0;
      }
    };
    
    // To and from string method using \n as splitter
    std::string 
    objectsToString() {
      std::string result;
      for(std::vector<std::string>::iterator it = objects_.begin(); it != objects_.end(); ++it) {
        result += *it;
        result += "\n";
      }
      return result;
    }
    
    static std::vector<std::string> stringToObjects(std::string str) {
      std::vector<std::string> objects;
      boost::split(objects, str, boost::is_any_of("\n"));
      // According to the insertion process above, we may have an empty string at the end
      if (objects.back() == "") {
        objects.pop_back();
      }
      return objects;
    }
    
    /**
     * Should-be-replaced methods end
     */
    
    /**
	 * PendingInterest class copied from ChronoSync2013 cpp implementation
	 *
	 * A PendingInterest holds an interest which onInterest received but could
	 * not satisfy. When we add a new data packet to the contentCache_, we will
	 * also check if it satisfies a pending interest.
	 */
	class PendingInterest {
	public:
	  /**
	   * Create a new PendingInterest and set the timeoutTime_ based on the current time and the interest lifetime.
	   * @param interest A shared_ptr for the interest.
	   * @param transport The transport from the onInterest callback. If the
	   * interest is satisfied later by a new data packet, we will send the data
	   * packet to the transport.
	   */
	  PendingInterest
		(const ptr_lib::shared_ptr<const Interest>& interest,
		 Transport& transport);

	  /**
	   * Return the interest given to the constructor.
	   */
	  const ptr_lib::shared_ptr<const Interest>&
	  getInterest() { return interest_; }

	  /**
	   * Return the transport given to the constructor.
	   */
	  Transport&
	  getTransport() { return transport_; }

	  /**
	   * Check if this interest is timed out.
	   * @param nowMilliseconds The current time in milliseconds from ndn_getNowMilliseconds.
	   * @return true if this interest timed out, otherwise false.
	   */
	  bool
	  isTimedOut(MillisecondsSince1970 nowMilliseconds)
	  {
		return timeoutTimeMilliseconds_ >= 0.0 && nowMilliseconds >= timeoutTimeMilliseconds_;
	  }

	private:
	  ptr_lib::shared_ptr<const Interest> interest_;
	  Transport& transport_;
	  MillisecondsSince1970 timeoutTimeMilliseconds_; /**< The time when the
		* interest times out in milliseconds according to ndn_getNowMilliseconds,
		* or -1 for no timeout. */
	};
	
  private:
    Name broadcastPrefix_;
    Name certificateName_;
    
    OnReceivedSyncData onReceivedSyncData_;
    
    Face& face_;
    MemoryContentCache contentCache_;
    
    KeyChain& keyChain_;
    
    // This serves as the rootDigest in ChronoSync.
    std::string currentDigest_;
    
    // This serves as the list of objects to be synchronized. 
    // For now, it's the list of full conference names (prefix + conferenceName)
    // Could be replaced with a Protobuf class or a class later.
    std::vector<std::string> objects_;
    
    // PendingInterestTable for holding outstanding interests.
    std::vector<ptr_lib::shared_ptr<PendingInterest> > pendingInterestTable_;
  };
}

#endif