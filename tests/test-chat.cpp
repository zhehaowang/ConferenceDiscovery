#include "external-observer.h"
#include "chrono-chat.h"

#include <exception>
#include <openssl/rand.h>
#include <stdio.h>

using namespace chrono_chat;
using namespace entity_discovery;
using namespace std;
using namespace ndn;

static const char *WHITESPACE_CHARS = " \n\r\t";

/**
 * Modify str in place to erase whitespace on the left.
 * @param str
 */
static inline void
trimLeft(string& str)
{
  size_t found = str.find_first_not_of(WHITESPACE_CHARS);
  if (found != string::npos) {
    if (found > 0)
      str.erase(0, found);
  }
  else
    // All whitespace
    str.clear();
}

/**
 * Modify str in place to erase whitespace on the right.
 * @param str
 */
static inline void
trimRight(string& str)
{
  size_t found = str.find_last_not_of(WHITESPACE_CHARS);
  if (found != string::npos) {
    if (found + 1 < str.size())
      str.erase(found + 1);
  }
  else
    // All whitespace
    str.clear();
}

/**
 * Modify str in place to erase whitespace on the left and right.
 * @param str
 */
static void
trim(string& str)
{
  trimLeft(str);
  trimRight(str);
}

/**
 * Read a line from from stdin and return a trimmed string.  (We don't use
 * cin because it ignores a blank line.)
 */
static string
stdinReadLine()
{
  char inputBuffer[1000];
  ssize_t nBytes = ::read(STDIN_FILENO, inputBuffer, sizeof(inputBuffer) - 1);
  if (nBytes < 0)
    // Don't expect an error reading from stdin.
    throw runtime_error("stdinReadLine: error reading from STDIN_FILENO");

  inputBuffer[nBytes] = 0;
  string input(inputBuffer);
  trim(input);

  return input;
}

/**
 * Poll stdin and return true if it is ready to ready (e.g. from stdinReadLine).
 */
static bool
isStdinReady()
{
  struct pollfd pollInfo;
  pollInfo.fd = STDIN_FILENO;
  pollInfo.events = POLLIN;

  return poll(&pollInfo, 1, 0) > 0;
}

static string
getRandomString()
{
  string seed("qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM0123456789");
  string result;
  uint8_t random[10];
  RAND_bytes(random, sizeof(random));
  for (int i = 0; i < 3; ++i) {
    // Using % means the distribution isn't uniform, but that's OK.
    size_t pos = (size_t)random[i] % seed.size();
    result += seed[pos];
  }

  return result;
}

class SampleChatObserver : public ChatObserver
{
  public:
    SampleChatObserver()
    {

    }

    void onStateChanged(chrono_chat::MessageTypes type, const char *prefix, const char *userName, const char *msg, double timestamp)
    {
      string state = "";
          switch (type) {
                case (chrono_chat::MessageTypes::JOIN): state = "Join"; break;
                case (chrono_chat::MessageTypes::CHAT): state = "Chat"; break;
                case (chrono_chat::MessageTypes::LEAVE):        state = "Leave"; break;
          }
          if (msg[0] != 0) {
            cout << state << " " << timestamp << "\t" << userName << " : " << msg << endl;
          }
          else {
            cout << state << " " << timestamp << "\t" << userName << endl;
          }
    }

    ptr_lib::shared_ptr<Chat> chat;
};

/**
 * Test function for chrono-chat
 */
int main()
{
  string screenName = getRandomString();
  string chatroom = "ndnchat";
  
  cout << "Please input hub prefix[/ndn/edu/ucla/remap]: " << endl;
  string hubPrefixString = "/ndn/edu/ucla/remap";
  hubPrefixString = stdinReadLine();
  if (hubPrefixString == "") {
    hubPrefixString = "/ndn/edu/ucla/remap";
  }
  
  Name hubPrefix(hubPrefixString);
  Name chatBroadcastPrefix("/ndn/broadcast/chrono-chat/");

  Face face;
  // Using default keyChain in ndn-cpp
  KeyChain keyChain;
  Name certificateName = keyChain.getDefaultCertificateName();

  face.setCommandSigningInfo(keyChain, keyChain.getDefaultCertificateName());
  std::vector<SampleChatObserver*> observers;
  // SampleChatObserver chatObserver;

  // This throws an libc++abi.dylib: terminate called throwing an exception
  // which is caused by "socket cannot connect to socket"
  int nChats = 1;
  try {
    // Instruction for using the class at chrono-chat.h
    for (int i = 0; i < nChats; i++) {
      observers.push_back(new SampleChatObserver());

      std::stringstream ss;
      ss << chatroom << i;

      observers[i]->chat.reset
        (new Chat(chatBroadcastPrefix, screenName, ss.str(),
           hubPrefix, observers[i],
           face, keyChain, certificateName));

      observers[i]->chat->start();
      usleep(500000);
    }
  }
  catch (std::exception& e) {
    cout << e.what() << '\n';
  }

  std::string msgString = "";
  
  while (1) {
    if (isStdinReady())
    {
      msgString = stdinReadLine();
      // resets chat without doing anything else, to test Peter's callback problem
      if (msgString == "-reset") {
        cout << "Chat deleted." << endl;
        observers[0]->chat->shutdown();
        observers[0]->chat.reset();
        continue;
      }
      if (msgString == "-join") {
        std::stringstream ss;
        ss << chatroom << 0;

        observers[0]->chat.reset
          (new Chat(chatBroadcastPrefix, screenName, ss.str(),
             hubPrefix, observers[0],
             face, keyChain, certificateName));

        observers[0]->chat->start();
        std::cout << "Chat started." << endl;

        continue;
      }
      if (msgString == "-roster") {
        std::vector<std::string> roster = observers[0]->chat->getRoster();
        for(std::vector<std::string>::iterator it = roster.begin(); it != roster.end(); ++it) {
          std::cout << *it << std::endl;
        }
        continue;
      }
      if (msgString == "-leave" || msgString == "-exit") {
        break;
      }
      if (msgString == "-auto") {
        for (int i = 0; i < 30; i++) {
          ostringstream ss;
          ss << i;
          observers[0]->chat->sendMessage(ss.str());
          for (int j = 0; j < 100; j++) {
                face.processEvents();
                usleep(2000);
          }
        }
        continue;
      }

      observers[0]->chat->sendMessage(msgString);
    }

    face.processEvents();
    usleep(10000);
  }
  // chatObserver.chat->leave();

  int sleepSeconds = 0;
  while (sleepSeconds < 1000000) {
    face.processEvents();
    usleep(10000);
    sleepSeconds += 10000;
  }

  return 1;
}