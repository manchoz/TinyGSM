/**
 * @file       TinyGsmClientSecureBG96.h
 * @author     Aurelien BOUIN
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2023 Aurelien BOUIN
 * @date       Aug 2023
 */

#ifndef SRC_TINYGSMCLIENTSECUREBG96_H_
#define SRC_TINYGSMCLIENTSECUREBG96_H_
// #pragma message("TinyGSM:  TinyGsmClientSecureBG96")

// #define TINY_GSM_DEBUG Serial

#define TINY_GSM_MUX_COUNT 12
#define TINY_GSM_BUFFER_READ_AND_CHECK_SIZE

#include "TinyGsmBattery.tpp"
#include "TinyGsmCalling.tpp"
#include "TinyGsmGPRS.tpp"
#include "TinyGsmGPS.tpp"
#include "TinyGsmModem.tpp"
#include "TinyGsmSMS.tpp"
#include "TinyGsmTCP.tpp"
#include "TinyGsmTemperature.tpp"
#include "TinyGsmTime.tpp"
#include "TinyGsmNTP.tpp"

#define GSM_NL "\r\n"
static const char GSM_OK[] TINY_GSM_PROGMEM    = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;
#if defined       TINY_GSM_DEBUG
static const char GSM_CME_ERROR[] TINY_GSM_PROGMEM = GSM_NL "+CME ERROR:";
static const char GSM_CMS_ERROR[] TINY_GSM_PROGMEM = GSM_NL "+CMS ERROR:";
#endif

enum RegStatus {
  REG_NO_RESULT    = -1,
  REG_UNREGISTERED = 0,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_OK_HOME      = 1,
  REG_OK_ROAMING   = 5,
  REG_UNKNOWN      = 4,
};

class TinyGsmBG96TLS : public TinyGsmModem<TinyGsmBG96TLS>,
                    public TinyGsmGPRS<TinyGsmBG96TLS>,
                    public TinyGsmTCP<TinyGsmBG96TLS, TINY_GSM_MUX_COUNT>,
                    public TinyGsmCalling<TinyGsmBG96TLS>,
                    public TinyGsmSMS<TinyGsmBG96TLS>,
                    public TinyGsmTime<TinyGsmBG96TLS>,
                    public TinyGsmNTP<TinyGsmBG96TLS>,
                    public TinyGsmGPS<TinyGsmBG96TLS>,
                    public TinyGsmBattery<TinyGsmBG96TLS>,
                    public TinyGsmTemperature<TinyGsmBG96TLS> {
  friend class TinyGsmModem<TinyGsmBG96TLS>;
  friend class TinyGsmGPRS<TinyGsmBG96TLS>;
  friend class TinyGsmTCP<TinyGsmBG96TLS, TINY_GSM_MUX_COUNT>;
  friend class TinyGsmCalling<TinyGsmBG96TLS>;
  friend class TinyGsmSMS<TinyGsmBG96TLS>;
  friend class TinyGsmTime<TinyGsmBG96TLS>;
  friend class TinyGsmNTP<TinyGsmBG96TLS>;
  friend class TinyGsmGPS<TinyGsmBG96TLS>;
  friend class TinyGsmBattery<TinyGsmBG96TLS>;
  friend class TinyGsmTemperature<TinyGsmBG96TLS>;

  /*
   * Inner Secure Client
   */
public:
  class GsmClientSecureBG96 : public GsmClient
  {
  friend class TinyGsmBG96TLS;

  public:
    GsmClientSecureBG96() {}

    explicit GsmClientSecureBG96(TinyGsmBG96TLS& modem, uint8_t mux = 0) {
      init(&modem, mux);
    }

    bool init(TinyGsmBG96TLS* modem, uint8_t mux = 0) {
      this->at       = modem;
      sock_available = 0;
      prev_check     = 0;
      sock_connected = false;
      got_data       = false;

      if (mux < TINY_GSM_MUX_COUNT) {
        this->mux = mux;
      } else {
        this->mux = (mux % TINY_GSM_MUX_COUNT);
      }
      at->sockets[this->mux] = this;

      return true;
    }

   public:
    virtual int connect(const char* host, uint16_t port, int timeout_s) {
      stop();
      TINY_GSM_YIELD();
      rx.clear();
      sock_connected = at->modemConnect(host, port, mux, true, timeout_s);
      return sock_connected;
    }
    TINY_GSM_CLIENT_CONNECT_OVERRIDES   

    void stop(uint32_t maxWaitMs) {
      uint32_t startMillis = millis();
      dumpModemBuffer(maxWaitMs);
      at->sendAT(GF("+QSSLCLOSE="), mux);
      sock_connected = false;
      at->waitResponse((maxWaitMs - (millis() - startMillis)));
    }
    void stop() override {
      stop(15000L);
    }

    /*
     * Extended API
     */

    String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;
  };

  /*
   * Constructor
   */
 public:
  explicit TinyGsmBG96TLS(Stream& stream) : stream(stream) {
    memset(sockets, 0, sizeof(sockets));
  }

  /*
   * Basic functions
   */
 protected:
  bool initImpl(const char* pin = NULL) {
    DBG(GF("### TinyGSM Version:"), TINYGSM_VERSION);
    DBG(GF("### TinyGSM Compiled Module:  TinyGsmClientSecureBG96"));

    if (!testAT()) { return false; }

    sendAT(GF("E0"));  // Echo Off
    if (waitResponse() != 1) { return false; }

#ifdef TINY_GSM_DEBUG
    sendAT(GF("+CMEE=2"));  // turn on verbose error codes
#else
    sendAT(GF("+CMEE=0"));  // turn off error codes
#endif
    waitResponse();

    DBG(GF("### Modem:"), getModemName());

    // Disable time and time zone URC's
    sendAT(GF("+CTZR=0"));
    if (waitResponse(10000L) != 1) { return false; }

    // Enable automatic time zone update
    sendAT(GF("+CTZU=1"));
    if (waitResponse(10000L) != 1) { return false; }
#if 1
    SimStatus ret = getSimStatus();
    // if the sim isn't ready and a pin has been provided, try to unlock the sim
    if (ret != SIM_READY && pin != NULL && strlen(pin) > 0) {
      simUnlock(pin);
      return (getSimStatus() == SIM_READY);
    } else {
      // if the sim is ready, or it's locked but no pin has been provided,
      // return true
      return (ret == SIM_READY || ret == SIM_LOCKED);
    }
#endif
  }
  /*
   * Power functions
   */
 protected:
  bool restartImpl(const char* pin = NULL) {
    if (!testAT()) { return false; }
    if (!setPhoneFunctionality(1, true)) { return false; }
    waitResponse(10000L, GF("APP RDY"));
    return init(pin);
  }

  bool powerOffImpl() {
    sendAT(GF("+QPOWD=1"));
    waitResponse(300);  // returns OK first
    return waitResponse(300, GF("POWERED DOWN")) == 1;
  }

  // When entering into sleep mode is enabled, DTR is pulled up, and WAKEUP_IN
  // is pulled up, the module can directly enter into sleep mode.If entering
  // into sleep mode is enabled, DTR is pulled down, and WAKEUP_IN is pulled
  // down, there is a need to pull the DTR pin and the WAKEUP_IN pin up first,
  // and then the module can enter into sleep mode.
  bool sleepEnableImpl(bool enable = true) {
    sendAT(GF("+QSCLK="), enable);
    return waitResponse() == 1;
  }

  bool setPhoneFunctionalityImpl(uint8_t fun, bool reset = false, uint32_t timeout_ms = 15500L) {
    sendAT(GF("+CFUN="), fun, reset ? ",1" : "");
    return waitResponse(timeout_ms, GF("OK")) == 1;
  }

  /*
   * Generic network functions
   */
 public:
  RegStatus getRegistrationStatus() {
    // Check first for EPS registration
    RegStatus epsStatus = (RegStatus)getRegistrationStatusXREG("CEREG");

    //if REG_UNKNOWN

    // If we're connected on EPS, great!
    if (epsStatus == REG_OK_HOME || epsStatus == REG_OK_ROAMING) {
      return epsStatus;
    } else {
      // Otherwise, check generic network status
      return (RegStatus)getRegistrationStatusXREG("CREG");
    }
  }

 protected:
  bool isNetworkConnectedImpl() {
    RegStatus s = getRegistrationStatus();
    return (s == REG_OK_HOME || s == REG_OK_ROAMING);
  }

  /*
   * GPRS functions
   */
 protected:
  bool gprsConnectImpl(const char* apn, const char* user = NULL,
                       const char* pwd = NULL) {
    gprsDisconnect();

    // Configure the TCPIP Context
    sendAT(GF("+QICSGP=1,1,\""), apn, GF("\",\""), user, GF("\",\""), pwd,
           GF("\""));
    if (waitResponse() != 1) { return false; }

    // Activate GPRS/CSD Context
    sendAT(GF("+QIACT=1"));
    if (waitResponse(150000L) != 1) { return false; }

    // Attach to Packet Domain service - is this necessary?
    sendAT(GF("+CGATT=1"));
    if (waitResponse(60000L) != 1) { return false; }

    return true;
  }

  bool gprsDisconnectImpl() {
    sendAT(GF("+QIDEACT=1"));  // Deactivate the bearer context
    if (waitResponse(40000L) != 1) { return false; }

    return true;
  }

  /*
   * SIM card functions
   */
 protected:
  String getSimCCIDImpl() {
    sendAT(GF("+QCCID"));
    if (waitResponse(GF(GSM_NL "+QCCID:")) != 1) { return ""; }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  /*
   * Phone Call functions
   */
 protected:
  // Can follow all of the phone call functions from the template

  /*
   * Messaging functions
   */
 protected:
  // Follows all messaging functions per template

  /*
   * GSM Location functions
   */
 protected:
  // NOTE:  As of application firmware version 01.016.01.016 triangulated
  // locations can be obtained via the QuecLocator service and accompanying AT
  // commands.  As this is a separate paid service which I do not have access
  // to, I am not implementing it here.



  /*
   * Testing functions
   */

 public:
  String getModemManufacturerIdentification(){
    sendAT(GF("+GMI"));
    String res;
    if (waitResponse(1000L, res) != 1) { return ""; }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }
  String getModemModelIdentification(){
    sendAT(GF("+GMM"));
    String res;
    if (waitResponse(1000L, res) != 1) { return ""; }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }
  String getModemFirmwareVersionIdentification(){
    sendAT(GF("+GMR"));
    String res;
    if (waitResponse(1000L, res) != 1) { return ""; }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }
  String getModemFirmwareFullVersionIdentification(){
    sendAT(GF("+QGMR"));
    String res;
    if (waitResponse(1000L, res) != 1) { return ""; }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.trim();
    return res;
  }
  String getModemFirmwareApplicationVersionIdentification(){
    sendAT(GF("+QAPPVER"));
    String res;
    if (waitResponse(1000L, res) != 1) { return ""; }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, " ");
    res.replace("+QAPPVER: ","");
    res.trim();
    return res;
  }
  bool configureGPS(void) {
    sendAT(GF("+QGPSCFG=\"gnssconfig\",5"));//Based on Mobile Country Code (europe : galileo)
    if (waitResponse() != 1) { return false; }
    return true;
  }
  bool configureGPSPriority(void) {
    sendAT(GF("+QGPSCFG=\"priority\""));//Priority GNSS instead of WWAN
    if (waitResponse() != 1) { return false; }
    return true;
  }
  bool test(void) {
    sendAT(GF("+QGPSCFG=\"gnssconfig\",5"));//Based on Mobile Country Code (europe : galileo)
    waitResponse();
    sendAT(GF("+QGPSCFG=\"priority\",0,0"));//Priority GNSS, not saved
    waitResponse();
    sendAT(GF("+QGPS=1,1,0,1"));//Low Accuracy, Continuous positionning, Rate : 1 sec
    waitResponse();
    sendAT(GF("+QGPSCFG=\"gnssconfig\",5"));//Based on Mobile Country Code (europe : galileo)
    waitResponse();
    sendAT(GF("+QGPSCFG=\"priority\",0,0"));//Priority GNSS, not saved
    waitResponse();
    return true;
  }
  bool test_ask(void) {
    sendAT(GF("+QGPSCFG=\"gnssconfig\""));//Based on Mobile Country Code (europe : galileo)
    waitResponse();
    //if (waitResponse() != 1) { return false; }
    sendAT(GF("+QGPSCFG=?"));
    waitResponse();
    sendAT(GF("+QGPSEND=?"));
    waitResponse();
    sendAT(GF("+QGPSEND"));
    waitResponse();
    return true;
  }
#define MODEM_RAT_SEARCHED_AUTOMATIC    (0)
#define MODEM_RAT_SEARCHED_GSM_ONLY     (1)
#define MODEM_RAT_SEARCHED_LTE_ONLY     (3)
#define MODEM_RAT_SEARCHED_ERROR        (255)
  uint8_t query_modem_rat_searched(void){
    uint8_t returned;
    sendAT(GF("+QCFG=\"nwscanmode\""));
    if (waitResponse(GF("+QCFG:")) != 1) { return MODEM_RAT_SEARCHED_ERROR; }
    streamSkipUntil('"');
    streamSkipUntil('"');
    streamSkipUntil(',');
    returned = stream.parseInt();
    waitResponse();
    return returned;
    /* Max Response Time : 300ms
     0 : Automatic (GSM + LTE)
     1 : GSM only
     3 : LTE only
     */
  }
#define MODEM_NETWORK_CATEGORY_SEARCHED_EMTC     (0)
#define MODEM_NETWORK_CATEGORY_SEARCHED_NBIOT    (1)
#define MODEM_NETWORK_CATEGORY_SEARCHED_ALL      (2)
#define MODEM_NETWORK_CATEGORY_SEARCHED_ERROR    (255)
  uint8_t query_modem_network_category_searched(void){
    uint8_t returned;
    sendAT(GF("+QCFG=\"iotopmode\""));
    if (waitResponse(GF("+QCFG:")) != 1) { return MODEM_NETWORK_CATEGORY_SEARCHED_ERROR; }
    streamSkipUntil('"');
    streamSkipUntil('"');
    streamSkipUntil(',');
    returned = stream.parseInt();
    waitResponse();
    return returned;
    /* Max Response Time : 300ms
     0 : eMTC
     1 : NB-IoT
     2 : eMTC and NB-IoT
     */
  }
  void query_modem_register_settings(void){
    sendAT(GF("+QCFG=\"band\""));
    waitResponse(500);
    sendAT(GF("+QCFG=\"nwscanmode\""));
    waitResponse(500);
    sendAT(GF("+QCFG=\"nwscanseq\""));
    waitResponse(500);
    sendAT(GF("+QCFG=\"iotopmode\""));
    waitResponse(500);
    /* Without any modification it gives :
    +QCFG: "band",0x3,0x80084,0x80084
    +QCFG: "nwscanmode",3
    +QCFG: "nwscanseq",020301
    +QCFG: "iotopmode",0
    When previously set automatic_register
    +QCFG: "band",0x3,0x80084,0x80084
    +QCFG: "nwscanmode",0
    +QCFG: "nwscanseq",020301
    +QCFG: "iotopmode",2

    */
  }
  void configure_europe_band(void){
    /*
     * =>Speed-up auto detection
     * Based on Quectel Series Network Searching Scheme Introduction 2.0-1.pdf
     * existing GSM band : GSM850, EGSM900, DCS1800, PCS1900
     * Europe GSM : EGSM900/DCS1800
     * EGSM900 : 0x1
     * DCS1800 : 0x2
     * =>3
     * F for all bands
     * Europe eMTC & NB-IOT bands : B3/B8/B20
     * B3 : 0x4
     * B8 : 0x80
     * B20 : 0x80000
     * =>80084
     */
    //sendAT(GF("+QCFG=\"band\",F,80084,80084,1"));
    sendAT(GF("+QCFG=\"band\",3,80084,80084,1"));
    waitResponse(500);
  }
  bool automatic_register(bool restart_modem = false) {
    //// NB-IOT Band Configuration
    //// 1NCE: 80 for Band 8
    //sendAT(GF("+QCFG=\"band\",0,0,80,1"));
    //waitResponse(500);

    // Configure RAT(s) to be Searched
    // 0 Automatic(GSM and LTE), 1 Take effect immediately(default)
    // 1 GSM only
    // 3 LTE only
    sendAT(GF("+QCFG=\"nwscanmode\",0,1"));
    waitResponse(500);

    // Configure RAT Searching Sequence
    //// (LTE Cat NB1 -> LTE Cat M1 -> GSM)
    //sendAT(GF("+QCFG=\"nwscanseq\",030201,1"));
    // 00 : (LTE Cat M1 -> LTE Cat NB1 -> GSM)(default)
    sendAT(GF("+QCFG=\"nwscanseq\",00,1"));
    waitResponse(500);

    // Configure Network Category to be Searched under 
    // 0 : LTE Cat M1
    // 1 : LTE Cat NB1
    // 2 : LTE Cat M1 and Cat NB1(default)
    //sendAT(GF("+QCFG=\"iotopmode\",1,1"));
    sendAT(GF("+QCFG=\"iotopmode\",2,1"));
    waitResponse(500);

    //// Configure Service Domain
    //// 1 : use PS domain service for data-service
    //// 2 : CS & PS (default)
    //sendAT(GF("+QCFG=\"servicedomain\",2,1"));
    //waitResponse(500);

    //// Configure Get the EMM cause value
    //// 0 : Numeric value (default)
    //// 1 : Verbose value
    //sendAT(GF("+QCFG=\"emmcause\",0"));
    //waitResponse(500);
    if(restart_modem){
      setPhoneFunctionalityImpl(0);
      setPhoneFunctionalityImpl(1);
    }

    return true;
  }
  bool force_ltem_register(bool restart_modem = false) {
    /* Purpose speed up searching time since much longer searching time for NB-IoT than eMTC
     * Based on Quectel Series Network Searching Scheme Introduction 2.0-1.pdf
     * 6.2. Solutions to Speed up Network Searching : 
     * 6.2.1 Disable NB-IoT and Enable Required RAT(s)
     */
    // Configure Network Category to be Searched under 
    // 0 : LTE Cat M1
    // 1 : LTE Cat NB1
    // 2 : LTE Cat M1 and Cat NB1(default)
    sendAT(GF("+QCFG=\"iotopmode\",0,1"));
    waitResponse(500);
    // Configure RAT(s) to be Searched
    // 0 Automatic(GSM and LTE), 1 Take effect immediately(default)
    // 1 GSM only
    // 3 LTE only
    sendAT(GF("+QCFG=\"nwscanmode\",3,1"));
    waitResponse(500);
    if(restart_modem){
      setPhoneFunctionalityImpl(0);
      setPhoneFunctionalityImpl(1);
    }
    return true;
  }
  bool force_nbiot_register(bool restart_modem = false) {
    /* Purpose speed up searching time since much longer searching time for NB-IoT than eMTC
     * Based on Quectel Series Network Searching Scheme Introduction 2.0-1.pdf
     * 6.2. Solutions to Speed up Network Searching : 
     * 6.2.2. Enable NB-IoT Bands Supported by Current Operator Only
     */
    // Configure Network Category to be Searched under 
    // 0 : LTE Cat M1
    // 1 : LTE Cat NB1
    // 2 : LTE Cat M1 and Cat NB1(default)
    sendAT(GF("+QCFG=\"iotopmode\",2,1"));
    waitResponse(500);
    // Configure RAT Searching Sequence
    //// (LTE Cat NB1 -> LTE Cat M1 -> GSM)
    //sendAT(GF("+QCFG=\"nwscanseq\",030201,1"));
    // 00 : (LTE Cat M1 -> LTE Cat NB1 -> GSM)(default)
    sendAT(GF("+QCFG=\"nwscanseq\",020301,1"));
    waitResponse(500);
    // Configure RAT(s) to be Searched
    // 0 Automatic(GSM and LTE), 1 Take effect immediately(default)
    // 1 GSM only
    // 3 LTE only
    sendAT(GF("+QCFG=\"nwscanmode\",3,1"));
    waitResponse(500);
    if(restart_modem){
      setPhoneFunctionalityImpl(0);
      setPhoneFunctionalityImpl(1);
    }
    return true;
  }
  bool force_gsm_register(bool restart_modem = false) {
    /* Purpose speed up searching time since much longer searching time for NB-IoT than eMTC
     * Based on Quectel Series Network Searching Scheme Introduction 2.0-1.pdf
     * 6.2. Solutions to Speed up Network Searching : 
     * 6.2.1 Disable NB-IoT and Enable Required RAT(s)
     */
    // Configure RAT(s) to be Searched
    // 0 Automatic(GSM and LTE), 1 Take effect immediately(default)
    // 1 GSM only
    // 3 LTE only
    sendAT(GF("+QCFG=\"nwscanmode\",1,1"));
    waitResponse(500);
    if(restart_modem){
      setPhoneFunctionalityImpl(0);
      setPhoneFunctionalityImpl(1);
    }
    return true;
  }
  bool lte_and_falling_gsm_register(bool restore_band = false, bool restart_modem = false) {
    if(restore_band){
      // Restore default Banc Configuration
      sendAT(GF("+QCFG=\"bandrestore\""));
      waitResponse(500);
    }
    // Configure Network Category to be Searched under 
    // 0 : LTE Cat M1
    // 1 : LTE Cat NB1
    // 2 : LTE Cat M1 and Cat NB1(default)
    sendAT(GF("+QCFG=\"iotopmode\",2,1"));
    waitResponse(500);
    // Configure RAT(s) to be Searched
    // 0 Automatic(GSM and LTE), 1 Take effect immediately(default)
    // 1 GSM only
    // 3 LTE only
    sendAT(GF("+QCFG=\"nwscanmode\",0,1"));
    waitResponse(500);
    // Configure RAT Searching Sequence
    //// (LTE Cat NB1 -> LTE Cat M1 -> GSM)
    //sendAT(GF("+QCFG=\"nwscanseq\",030201,1"));
    // 00 : (LTE Cat M1 -> LTE Cat NB1 -> GSM)(default)
    // 01 : GSM
    // 02 : LTE-M(eMTC)
    // 03 : NB-IOT
    sendAT(GF("+QCFG=\"nwscanseq\",020301,1"));
    waitResponse(500);
    if(restart_modem){
      setPhoneFunctionalityImpl(0);
      setPhoneFunctionalityImpl(1);
    }
    return true;
  }
  /*
   * parse AT+COPS :
   * +COPS: <mode>[,<format>[,<oper>][,<Act>]]
   * where <Act>:
   * 0 : GSM
   * 8 : eMTC/LTE-M
   * 9 : NB-IoT
   */
#define MODEM_COP_GSM     (0)
#define MODEM_COP_EMTC    (8)
#define MODEM_COP_NBIOT   (9)
  uint8_t get_access_technology(void) {
    uint8_t act_value;
    sendAT(GF("+COPS?"));
    if (waitResponse(GF("+COPS:")) != 1) { return 10; }
    streamSkipUntil('"'); /* Skip mode and format */
    streamSkipUntil('"'); /* Skip mode and format */
    streamSkipUntil(','); /* Skip mode and format */
    act_value = stream.parseInt();
    waitResponse();
    return act_value;
  }
  void get_emmcause(void) {
    uint8_t act_value;
    sendAT(GF("+QCFG=\"emmcause\",1"));
    waitResponse();
  }
  bool ask_to_automatically_register(void) {
    sendAT(GF("+COPS=0"));  // Auto-register to the network
    if (waitResponse() != 1) { return false; }
    return true;
  }

  /*
   * GPS/GNSS/GLONASS location functions
   */
 protected:
  // enable GPS
  bool enableGPSImpl() {
    sendAT(GF("+QGPS=1"));
    if (waitResponse() != 1) { return false; }
    return true;
  }

  bool disableGPSImpl() {
    sendAT(GF("+QGPSEND"));
    if (waitResponse() != 1) { return false; }
    return true;
  }

  // get the RAW GPS output
  String getGPSrawImpl() {
    sendAT(GF("+QGPSLOC=2"));
    if (waitResponse(10000L, GF(GSM_NL "+QGPSLOC:")) != 1) { return ""; }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  // get GPS informations
  bool getGPSImpl(float* lat, float* lon, float* speed = 0, float* alt = 0,
                  int* vsat = 0, int* usat = 0, float* accuracy = 0,
                  int* year = 0, int* month = 0, int* day = 0, int* hour = 0,
                  int* minute = 0, int* second = 0) {
    sendAT(GF("+QGPSLOC=2"));
    if (waitResponse(10000L, GF(GSM_NL "+QGPSLOC: ")) != 1) {
      // NOTE:  Will return an error if the position isn't fixed
      return false;
    }

    // init variables
    float ilat         = 0;
    float ilon         = 0;
    float ispeed       = 0;
    float ialt         = 0;
    int   iusat        = 0;
    float iaccuracy    = 0;
    int   iyear        = 0;
    int   imonth       = 0;
    int   iday         = 0;
    int   ihour        = 0;
    int   imin         = 0;
    float secondWithSS = 0;

    // UTC date & Time
    ihour        = streamGetIntLength(2);      // Two digit hour
    imin         = streamGetIntLength(2);      // Two digit minute
    secondWithSS = streamGetFloatBefore(',');  // 6 digit second with subseconds

    ilat      = streamGetFloatBefore(',');  // Latitude
    ilon      = streamGetFloatBefore(',');  // Longitude
    iaccuracy = streamGetFloatBefore(',');  // Horizontal precision
    ialt      = streamGetFloatBefore(',');  // Altitude from sea level
    streamSkipUntil(',');                   // GNSS positioning mode
    streamSkipUntil(',');  // Course Over Ground based on true north
    streamSkipUntil(',');  // Speed Over Ground in Km/h
    ispeed = streamGetFloatBefore(',');  // Speed Over Ground in knots

    iday   = streamGetIntLength(2);    // Two digit day
    imonth = streamGetIntLength(2);    // Two digit month
    iyear  = streamGetIntBefore(',');  // Two digit year

    iusat = streamGetIntBefore(',');  // Number of satellites,
    streamSkipUntil('\n');  // The error code of the operation. If it is not
                            // 0, it is the type of error.

    // Set pointers
    if (lat != NULL) *lat = ilat;
    if (lon != NULL) *lon = ilon;
    if (speed != NULL) *speed = ispeed;
    if (alt != NULL) *alt = ialt;
    if (vsat != NULL) *vsat = 0;
    if (usat != NULL) *usat = iusat;
    if (accuracy != NULL) *accuracy = iaccuracy;
    if (iyear < 2000) iyear += 2000;
    if (year != NULL) *year = iyear;
    if (month != NULL) *month = imonth;
    if (day != NULL) *day = iday;
    if (hour != NULL) *hour = ihour;
    if (minute != NULL) *minute = imin;
    if (second != NULL) *second = static_cast<int>(secondWithSS);

    waitResponse();  // Final OK
    return true;
  }

  /*
   * Time functions
   */
 protected:
  String getGSMDateTimeImpl(TinyGSMDateTimeFormat format) {
    sendAT(GF("+QLTS=2"));
    if (waitResponse(2000L, GF("+QLTS: \"")) != 1) { return ""; }

    String res;

    switch (format) {
      case DATE_FULL: res = stream.readStringUntil('"'); break;
      case DATE_TIME:
        streamSkipUntil(',');
        res = stream.readStringUntil('"');
        break;
      case DATE_DATE: res = stream.readStringUntil(','); break;
    }
    waitResponse();  // Ends with OK
    return res;
  }

  // The BG96 returns UTC time instead of local time as other modules do in
  // response to CCLK, so we're using QLTS where we can specifically request
  // local time.
  bool getNetworkUTCTimeImpl(int* year, int* month, int* day, int* hour,
                          int* minute, int* second, float* timezone) {
    sendAT(GF("+QLTS=1"));
    if (waitResponse(2000L, GF("+QLTS: \"")) != 1) { return false; }

    int iyear     = 0;
    int imonth    = 0;
    int iday      = 0;
    int ihour     = 0;
    int imin      = 0;
    int isec      = 0;
    int itimezone = 0;

    // Date & Time
    iyear       = streamGetIntBefore('/');
    imonth      = streamGetIntBefore('/');
    iday        = streamGetIntBefore(',');
    ihour       = streamGetIntBefore(':');
    imin        = streamGetIntBefore(':');
    isec        = streamGetIntLength(2);
    char tzSign = stream.read();
    itimezone   = streamGetIntBefore(',');
    if (tzSign == '-') { itimezone = itimezone * -1; }
    streamSkipUntil('\n');  // DST flag

    // Set pointers
    if (iyear < 2000) iyear += 2000;
    if (year != NULL) *year = iyear;
    if (month != NULL) *month = imonth;
    if (day != NULL) *day = iday;
    if (hour != NULL) *hour = ihour;
    if (minute != NULL) *minute = imin;
    if (second != NULL) *second = isec;
    if (timezone != NULL) *timezone = static_cast<float>(itimezone) / 4.0;

    // Final OK
    waitResponse();  // Ends with OK
    return true;
  }

  // The BG96 returns UTC time instead of local time as other modules do in
  // response to CCLK, so we're using QLTS where we can specifically request
  // local time.
  bool getNetworkTimeImpl(int* year, int* month, int* day, int* hour,
                          int* minute, int* second, float* timezone) {
    sendAT(GF("+QLTS=2"));
    if (waitResponse(2000L, GF("+QLTS: \"")) != 1) { return false; }

    int iyear     = 0;
    int imonth    = 0;
    int iday      = 0;
    int ihour     = 0;
    int imin      = 0;
    int isec      = 0;
    int itimezone = 0;

    // Date & Time
    iyear       = streamGetIntBefore('/');
    imonth      = streamGetIntBefore('/');
    iday        = streamGetIntBefore(',');
    ihour       = streamGetIntBefore(':');
    imin        = streamGetIntBefore(':');
    isec        = streamGetIntLength(2);
    char tzSign = stream.read();
    itimezone   = streamGetIntBefore(',');
    if (tzSign == '-') { itimezone = itimezone * -1; }
    streamSkipUntil('\n');  // DST flag

    // Set pointers
    if (iyear < 2000) iyear += 2000;
    if (year != NULL) *year = iyear;
    if (month != NULL) *month = imonth;
    if (day != NULL) *day = iday;
    if (hour != NULL) *hour = ihour;
    if (minute != NULL) *minute = imin;
    if (second != NULL) *second = isec;
    if (timezone != NULL) *timezone = static_cast<float>(itimezone) / 4.0;

    // Final OK
    waitResponse();  // Ends with OK
    return true;
  }

  /*
   * NTP server functions
   */

  byte NTPServerSyncImpl(String server = "pool.ntp.org", byte = -5) {
    // Request network synchronization
    // AT+QNTP=<contextID>,<server>[,<port>][,<autosettime>]
    sendAT(GF("+QNTP=1,\""), server, '"');
    if (waitResponse(10000L, GF("+QNTP:"))) {
      String result = stream.readStringUntil(',');
      streamSkipUntil('\n');
      result.trim();
      if (TinyGsmIsValidNumber(result)) { return result.toInt(); }
    } else {
      return -1;
    }
    return -1;
  }

  String ShowNTPErrorImpl(byte error) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  /*
   * Battery functions
   */
 protected:
  // Can follow CBC as in the template

  /*
   * Temperature functions
   */
 protected:
  // get temperature in degree celsius
  uint16_t getTemperatureImpl() {
    sendAT(GF("+QTEMP"));
    if (waitResponse(GF(GSM_NL "+QTEMP:")) != 1) { return 0; }
    // return temperature in C
    uint16_t res =
        streamGetIntBefore(',');  // read PMIC (primary ic) temperature
    streamSkipUntil(',');         // skip XO temperature ??
    streamSkipUntil('\n');        // skip PA temperature ??
    // Wait for final OK
    waitResponse();
    return res;
  }

  /*
   * Client related functions
   */
 protected:
  bool modemConnect(const char* host, uint16_t port, uint8_t mux,
                    bool ssl = false, int timeout_s = 150) {

    uint32_t timeout_ms = ((uint32_t)timeout_s) * 1000;

      // set the ssl version
      // AT+QSSLCFG="sslversion",<ctxindex>,<sslversion>
      // <ctxindex> PDP context identifier
      // <sslversion> 0: QAPI_NET_SSL_3.0
      //              1: QAPI_NET_SSL_PROTOCOL_TLS_1_0
      //              2: QAPI_NET_SSL_PROTOCOL_TLS_1_1
      //              3: QAPI_NET_SSL_PROTOCOL_TLS_1_2
      //              4: ALL
      // NOTE:  despite docs using caps, "sslversion" must be in lower case
      sendAT(GF("+QSSLCFG=\"sslversion\",0,4"));  // TLS 1.2
      if (waitResponse(5000L) != 1) return false;
      // set the ssl cipher_suite
      // AT+QSSLCFG="ciphersuite",<ctxindex>,<cipher_suite>
      // <ctxindex> PDP context identifier
      // <cipher_suite> 0: TODO
      //              1: TODO
      //              0X0035: TLS_RSA_WITH_AES_256_CBC_SHA
      //              0XFFFF: ALL
      // NOTE:  despite docs using caps, "sslversion" must be in lower case
      sendAT(GF("+QSSLCFG=\"ciphersuite\",0,0XFFFF"));  // TLS_RSA_WITH_AES_256_CBC_SHA
      if (waitResponse(5000L) != 1) return false;
      // set the ssl sec level
      // AT+QSSLCFG="seclevel",<ctxindex>,<sec_level>
      // <ctxindex> PDP context identifier
      // <sec_level> 0: TODO
      //              1: TODO
      //              0X0035: TLS_RSA_WITH_AES_256_CBC_SHA
      //              0XFFFF: ALL
      // NOTE:  despite docs using caps, "sslversion" must be in lower case
      sendAT(GF("+QSSLCFG=\"seclevel\",0,2"));
      if (waitResponse(5000L) != 1) return false;


      if (certificates[mux] != "") {
        // apply the correct certificate to the connection
        // AT+QSSLCFG="cacert",<ctxindex>,<caname>
        // <ctxindex> PDP context identifier
        // <certname> certificate name

        //sendAT(GF("+CASSLCFG="), mux, ",CACERT,\"", certificates[mux].c_str(),
        //       "\"");
        sendAT(GF("+QSSLCFG=\"cacert\",0,\""), certificates[mux].c_str(), GF("\""));
        if (waitResponse(5000L) != 1) return false;

      }

    // <PDPcontextID>(1-16), <connectID>(0-11),
    // "TCP/UDP/TCP LISTENER/UDPSERVICE", "<IP_address>/<domain_name>",
    // <remote_port>,<local_port>,<access_mode>(0-2; 0=buffer)
    //may need previous AT+QSSLCFG
    sendAT(GF("+QSSLOPEN=1,1,"), mux, GF(",\""), host,
          GF("\","), port, GF(",0"));
    waitResponse();

    if (waitResponse(timeout_ms, GF(GSM_NL "+QSSLOPEN:")) != 1) { return false; }
    //20230629 -> +QSSLOPEN: <clientID>,<err>
    //clientID is mux
    //err must be 0
    if (streamGetIntBefore(',') != mux) { return false; }
    // Read status
    return (0 == streamGetIntBefore('\n'));
  }

  int16_t modemSend(const void* buff, size_t len, uint8_t mux) {
    sendAT(GF("+QSSLSEND="), mux, ',', (uint16_t)len);
    if (waitResponse(GF(">")) != 1) { return 0; }
    stream.write(reinterpret_cast<const uint8_t*>(buff), len);
    stream.flush();
    if (waitResponse(GF(GSM_NL "SEND OK")) != 1) { return 0; }
    // TODO(?): Wait for ACK? AT+QSSLSEND=id,0
    return len;
  }

  size_t modemRead(size_t size, uint8_t mux) {
    if (!sockets[mux]) return 0;
    sendAT(GF("+QSSLRECV="), mux, ',', (uint16_t)size);
    if (waitResponse(GF("+QSSLRECV:")) != 1) {
      DBG("### READ: For unknown reason close");
      return 0;
    }
    int16_t len = streamGetIntBefore('\n');

    for (int i = 0; i < len; i++) {
      moveCharFromStreamToFifo(mux);
      }
    waitResponse();
    sockets[mux]->sock_available = modemGetAvailable(mux);
    return len;
  }

  size_t modemGetAvailable(uint8_t mux) {
    if (!sockets[mux]) return 0;
    sendAT(GF("+QSSLRECV="), mux, GF(",0"));
    size_t result = 0;
    if (waitResponse(GF("+QSSLRECV:")) == 1) {
      streamSkipUntil(',');  // Skip total received
      streamSkipUntil(',');  // Skip have read
      result = streamGetIntBefore('\n');
      if (result) { DBG("### DATA AVAILABLE:", result, "on", mux); }
      waitResponse();
    }
    //if (!result) { sockets[mux]->sock_connected = modemGetConnected(mux); }
    return result;
  }

  bool modemGetConnected(uint8_t mux) {
    sendAT(GF("+QSSLSTATE=1,"), mux);
    // +QSSLSTATE: 0,"TCP","151.139.237.11",80,5087,4,1,0,0,"uart1"

    if (waitResponse(GF("+QSSLSTATE:")) != 1) { return false; }

    streamSkipUntil(',');                  // Skip mux
    streamSkipUntil(',');                  // Skip socket type
    streamSkipUntil(',');                  // Skip remote ip
    streamSkipUntil(',');                  // Skip remote port
    streamSkipUntil(',');                  // Skip local port
    int8_t res = streamGetIntBefore(',');  // socket state

    waitResponse();

    // 0 Initial, 1 Opening, 2 Connected, 3 Listening, 4 Closing
    return 2 == res;
  }

  /*
   * Utilities
   */
 public:
  // TODO(vshymanskyy): Optimize this!
  int8_t waitResponse(uint32_t timeout_ms, String& data,
                      GsmConstStr r1 = GFP(GSM_OK),
                      GsmConstStr r2 = GFP(GSM_ERROR),
#if defined TINY_GSM_DEBUG
                      GsmConstStr r3 = GFP(GSM_CME_ERROR),
                      GsmConstStr r4 = GFP(GSM_CMS_ERROR),
#else
                      GsmConstStr r3 = NULL, GsmConstStr r4 = NULL,
#endif
                      GsmConstStr r5 = NULL) {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    uint8_t  index       = 0;
    uint32_t startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        TINY_GSM_YIELD();
        int8_t a = stream.read();
        if (a <= 0) continue;  // Skip 0x00 bytes, just in case
        data += static_cast<char>(a);
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
#if defined TINY_GSM_DEBUG
          if (r3 == GFP(GSM_CME_ERROR)) {
            streamSkipUntil('\n');  // Read out the error
          }
#endif
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF(GSM_NL "+QIURC:"))) {
          streamSkipUntil('\"');
          String urc = stream.readStringUntil('\"');
          streamSkipUntil(',');
          if (urc == "recv") {
            int8_t mux = streamGetIntBefore('\n');
            DBG("### URC RECV:", mux);
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->got_data = true;
            }
          } else if (urc == "closed") {
            int8_t mux = streamGetIntBefore('\n');
            DBG("### URC CLOSE:", mux);
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->sock_connected = false;
            }
          } else {
            streamSkipUntil('\n');
          }
          data = "";
        }
      }
    } while (millis() - startMillis < timeout_ms);
  finish:
    if (!index) {
      data.trim();
      if (data.length() && data!="RDY") { DBG("### Unhandled:", data); }
      data = "";
    }
    // data.replace(GSM_NL, "/");
    // DBG('<', index, '>', data);
    return index;
  }

  int8_t waitResponse(uint32_t timeout_ms, GsmConstStr r1 = GFP(GSM_OK),
                      GsmConstStr r2 = GFP(GSM_ERROR),
#if defined TINY_GSM_DEBUG
                      GsmConstStr r3 = GFP(GSM_CME_ERROR),
                      GsmConstStr r4 = GFP(GSM_CMS_ERROR),
#else
                      GsmConstStr r3 = NULL, GsmConstStr r4 = NULL,
#endif
                      GsmConstStr r5 = NULL) {
    String data;
    return waitResponse(timeout_ms, data, r1, r2, r3, r4, r5);
  }

  int8_t waitResponse(GsmConstStr r1 = GFP(GSM_OK),
                      GsmConstStr r2 = GFP(GSM_ERROR),
#if defined TINY_GSM_DEBUG
                      GsmConstStr r3 = GFP(GSM_CME_ERROR),
                      GsmConstStr r4 = GFP(GSM_CMS_ERROR),
#else
                      GsmConstStr r3 = NULL, GsmConstStr r4 = NULL,
#endif
                      GsmConstStr r5 = NULL) {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

 public:
  Stream& stream;

 protected:
  GsmClientSecureBG96* sockets[TINY_GSM_MUX_COUNT];
  const char*    gsmNL = GSM_NL;
  String         certificates[TINY_GSM_MUX_COUNT];
};

#endif  // SRC_TINYGSMCLIENTSECUREBG96_H_
