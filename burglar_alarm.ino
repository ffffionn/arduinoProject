/*
 * Alarm functionality with the Arduino.
 * Authors: Fionn O'Connor    (ID: 112452278)
 *          Alexey Shapovalov (ID: )
 *          Ciaran McKenzie   (ID: )
 */

#include <IRremote.h>
#include <PinChangeInt.h>
#include <Time.h>
#include <LiquidCrystal.h>
#include <stdio.h>
#include <stdarg.h>
#include <EEPROM.h>
#include <string.h>

//LCD setup pins
#define RS 7
#define E 8
#define D4 9
#define D5 10
#define D6 11
#define D7 12

//Infrared Remote HEX codes
#define POW  0xFFA25D
#define MODE 0xFF629D
#define MUTE 0xFFE21D
#define PREV 0xFF22DD
#define NEXT 0xFF02FD
#define PLAY 0xFFC23D
#define MINUS 0xFFE01F
#define PLUS 0xFFA857
#define EQ 0xFF906F
#define ZERO 0xFF6897
#define HUNDRED 0xFF9867
#define RET 0xFFB04F
#define ONE 0xFF30CF
#define TWO 0xFF18E7
#define THREE 0xFF7A85
#define FOUR 0xFF10EF
#define FIVE 0xFF38C7
#define SIX 0xFF5AA5
#define SEVEN 0xFF42BD
#define EIGHT 0xFF4AB5
#define NINE 0xFF52AD

//Misc. defines
#define ENGINEER_PASS 368   // master password that can't be reset
#define IR_PIN 0            // pin for infrared remote
#define BUZZER 2            // pin for sounding the alarm
#define LED_PIN 3           // pin for the led
#define PASS_LENGTH 3       // engineer pass should be changed to match this

//ZONE TYPES
#define ENTRYEXIT 1
#define ANALOGUE 2
#define DIGITAL 3
#define CONTINUOUS 4

//ZONE DETECTION PINS
#define ZONE1 14
#define ZONE2 15
#define ZONE3 16
#define ZONE4 17


typedef struct Zone Zone;
typedef struct ModeSettings ModeSettings;

struct Zone {
  int zoneNumber;
  int mode;
  boolean isActive;
  int pin;
  struct ModeSettings {
    int entryExitTime;
    int digitalSignalType;
    int threshold;
  }  settings;
};



//GLOBAL VARIABLES
decode_results results;      //results.value == IR receiver code in hex
boolean alarmTriggered;      //global indicator whether the alarm is on
int USER_PASS;               //the user's password
int ALARM_TIME;              //set depending on ModeSettings for EntryExitTime
volatile byte alarm;         //lower NUM_ZONES bits used for indicating if a zone alarm has been triggered
volatile byte seconds = 0;   //seconds counter used for entry/exit mode
volatile boolean validated;  //to indicate if the owner has tripped the alarm


//         #  Mode       Active?  Pin     sec  edge  threshold 
Zone z1 = {1, ENTRYEXIT,  true,  ZONE1,  { 10, RISING, 45 }  };
Zone z2 = {2, DIGITAL,    true,  ZONE2,  { 20, FALLING, 15 } };
Zone z3 = {3, CONTINUOUS, true,  ZONE3,  { 5, FALLING, 80 }  };
Zone z4 = {4, ANALOGUE,   true,  ZONE4,  { 15, NULL, 50 }    };

#define NUM_ZONES 4
Zone zoneArray[NUM_ZONES] = { z1, z2, z3, z4 };


LiquidCrystal lcd(RS, E, D4, D5, D6, D7);
IRrecv irrecv(IR_PIN);



/*
 * Initialise variables and pin modes.
 */
void setup() {
//Setup I/O
  lcd.begin(20, 2);
  Serial.begin(9600);
  lcd.clear();
  irrecv.enableIRIn(); // Start the receiver
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  if( EEPROM.read(0) == 255 ){
    EEPROM.write(0,32); // set default log location to after zone settings
  }  

//read from EEProm here to initialise settings to previous ones and setupZones
  if( EEPROM.read(3) != 255 ){ 
    Serial.println( F("User settings detected") );
    loadSettings();
  }else{
    setDefaults();
  }
  
  setupZones();  // if active, pins will be registered as external interrupts

//Set up Timer interrupt for entry/exit zone
  TCCR1A = 0;
  TCCR1B = 0;
  OCR1A = 15625;
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS10);
  TCCR1B |= (1 << CS12);
  TIMSK1 |= (0 << OCIE1A); //Make sure timer is off initially
  sei();

  alarm = B00000000;
  
  setDateTime(); 
}


/*
 * Set default settings if there is no settings stored in EEProm.
 * All zones are unactive by default. 
 */
void setDefaults(){

  USER_PASS = 555;  //define default password

  int[] defaultZoneNums = { 1, 2, 3, 4 };
  int[] defaultModes = { ENTRYEXIT, DIGITAL, CONTINUOUS, CONTINUOUS };
  int[] defaultPins = { ZONE1, ZONE2, ZONE3, ZONE4 };
  int[] defaultTimes = { 10, 20, 5, 15 };
  int[] defaultEdges = { RISING, HIGH, FALLING, HIGH };
  int[] defaultThresholds = { 45, 15, 80, 60 };

//apply defaults to first 4 zones
  for( int i = 0; i < 4; i++ ){
    &zoneArray[i]->zoneNumber = defaultZoneNums[i];
    &zoneArray[i]->mode = defaultZoneNums[i];
    &zoneArray[i]->isActive = false;
    &zoneArray[i]->pin = defaultPins[i];
    &zoneArray[i]->settings.entryExitTime = defaultTimes[i];
    &zoneArray[i]->settings.digitalSignalType = defaultEdges[i];
    &zoneArray[i]->settings.threshold = defaultThresholds[i];
  }
  
}


/*
 * Alarm on - TRUE. Alarm off - FALSE.
 */
void toggleAlarm( boolean toToggle ){
  digitalWrite(BUZZER, toToggle);
  digitalWrite(LED_PIN, toToggle);
  alarmTriggered = toToggle;
  
  if(alarmTriggered){
    Serial.println( F("Alarm On") );
  }else{
    Serial.println( F("Alarm Off") );
  }
}



/*
 * Start the alarm and wait for password to be entered.
 * If password checks out, turn alarm off. Else get more input.
 */
void validateAlarm(){
  lcd.clear();
  lcd.print( F("Enter password:") );
  lcd.setCursor(0,1);

  //while alarm's on, check for password
  while( alarmTriggered ){
   if( passwordCheck( USER_PASS ) ){
       toggleAlarm( false );
       validated = true;
    }else{
       lcd.setCursor(0,1);
       delay(300);
    }
  }

  lcd.clear();
}


/*
 * Main loop of the program. Polls analog and decodes the alarm to see has a zone 
 * been triggered. Also prints the date and time to the lcd.
 */
void loop() {

  checkAnalogueZones();          //poll any analogue zones
  decodeAlarm();                 //decode alarm bits 
  
  if (irrecv.decode(&results)) {
    handleIR(results);           //handle user input
    irrecv.resume();
  }
  
  lcd.setCursor(2,0);            //center date
  printDate();
  lcd.setCursor(4, 1);           //center time
  printTime();
}





  


            /**********************************************\
                           IR and Menu Functions         
            \**********************************************/


/*
 * Handles the function to be performed by the system through the IR remote
 */
void handleIR(decode_results button){
  
  switch( button.value ){

 //Manually toggle the alarm off  (DEBUG)
     case( POW ): saveSettings();
                  lcd.clear(); 
                  lcd.print("Settings saved!");
                  delay(800);
                  lcd.clear();
                  break;

 //Choose a Zone 1-4 and change it's mode.
     case( MODE ): selectMode( selectZone() );
                   break;

  //Clear the EEPROM memory
     case( MUTE ): clearMemory();
                   break;

  //print Zone info to serial
    case( MINUS ):  readLog();
                    break;

 //show zone Stats on lcd for a given zone
     case( PLUS ): showZoneStats( selectZone() );
                   break;

 // Activte/deactivate a zone
     case( PLAY ): toggleZone( selectZone() );
                   break;

 // Set the Date and Time;
    case( PREV ): setDateTime();
                   break;


 //Change the user password. Previous password must be entered first.
    case( NEXT ): if( changePassword() ){
                      lcd.clear();
                      lcd.print( F("Password changed!") );
                      lcd.setCursor(0, 1);
                      lcd.print("-->");
                      lcd.print(USER_PASS);
                      delay(1500);
                      lcd.clear();
                    }
                    break;

                   
  //Pick a zone and check which mode it's running
    case( EQ ):  changeSettings( selectZone() );
                  break;

  //Load user settings from EEPROM
    case( RET ): loadSettings();
                   lcd.clear();
                   lcd.print("Settings loaded");
                   delay(800);
                   lcd.clear();
                   break;
          
  //Pick zone to show zone info (on Serial)
     case( ZERO ): zoneInfo(selectZone());
                    break;
                   
     case( ONE ): showZoneStats( &zoneArray[0] );
                  break;
     case( TWO ): showZoneStats( &zoneArray[1] );
                  break;
     case( THREE ): showZoneStats( &zoneArray[2] );
                    break;
     case( FOUR ): showZoneStats( &zoneArray[3] );
                   break;

     default: break;
  } 
}

/*
 * Print all zone info for a given zone.
 */
void zoneInfo( Zone *zone){
  Zone z = *(zone);
  Serial.print( F("**** ZONE ") );
  Serial.println( z.zoneNumber );
  Serial.print( F("Mode: ") );
  Serial.println( z.mode );
          
  Serial.print("Pin: ");
  Serial.println( z.pin );
      
  Serial.print( F("Active: ") );
  Serial.println( z.isActive ? "Yes" : "No" );
      
  Serial.print( F("Entry/Exit Timer:") );
  Serial.println( z.settings.entryExitTime );
          
  Serial.print( F("Digital Signal Type:")  );
  Serial.println( z.settings.digitalSignalType );
          
  Serial.print( F("Analogue Threshold: ") );
  Serial.println( z.settings.threshold );
  Serial.println("*****");
}


/*
 * Change the setting for the mode a zone is in
 */
void changeSettings( Zone *z ){

  lcd.clear();
  int input;

  switch( z->mode ){
    case( ENTRYEXIT ):  lcd.print( F("Entry/Exit time:") );
                        do{
                          lcd.setCursor(0,1); 
                          input = getIntWithLength(2);
                        }while( input > 0 );
                        z->settings.entryExitTime = input;
                        break;
    case( ANALOGUE ):  lcd.print("Enter threshold");
                       do{
                         lcd.setCursor(0,1); 
                         input = getIntWithLength(2);
                       }while( input > 100 );
                       z->settings.threshold = input;
                       break;
                       
    case( DIGITAL ): lcd.print("Enter edge type: (1-4)");
                      do{ 
                        lcd.setCursor( 0, 1 );
                        input = getIntWithLength(1);
                      }while( input > 4 );
                      z->settings.digitalSignalType = input; 
                      break;
  }

  lcd.clear();
  lcd.print("Setting changed");
  delay(2000);
  lcd.clear();
}


/*
 * Returns the setting value for a zone depending on the mode
 */ 
int settingForMode( Zone *ptr ){
  switch( z->mode ){
    case( ENTRYEXIT ):  return z->settings.entryExitTime;
                        break;
    case( ANALOGUE ):  return z->settings.threshold;
                       break;
    case( DIGITAL ):  return z->settings.digitalSignalType; 
                      break;
  }
}

/*
 * Toggle a zone from active to unactive or vica-versa
 */
void toggleZone( Zone *z ){
  Serial.print(z->zoneNumber);
  if( z->isActive ){
    z->isActive = false;
    disableInterrupt( z );
    Serial.println(" enabled");
  }else{
    z->isActive = true;
    enableInterrupt(z);
    Serial.println(" disabled");
  }
}


/*
 * Show info about the zone on the lcd
 */
void showZoneStats( Zone *z ){
  lcd.clear();
  lcd.print("Zone ");
  lcd.print(z->zoneNumber);
  lcd.print(", mode ");
  lcd.print(z->mode);
  lcd.print( z->isActive ? " !" : " X" );
  lcd.setCursor(0, 1);
  lcd.print("On:");
  lcd.print( z->isActive ? "Y" : "N" );
  lcd.print(" Pin:");
  lcd.print(z->pin);



  delay(3500);
  lcd.clear();
}

/*
 * Returns true if the password is changed successfully. One chance to get right.
 */
boolean changePassword(){
  lcd.clear();
  lcd.print( F("Enter old pass:") );
  lcd.setCursor(0,1);
  
  if( passwordCheck( USER_PASS ) ){  // password is correct, enter new password.  
    lcd.clear();
    lcd.print( F("New password [") );
    lcd.print( PASS_LENGTH );
    lcd.print( F("]") );
    lcd.setCursor(0,1);
    USER_PASS = getIntWithLength(PASS_LENGTH);
    return true;
  }else{                   // Incorrect credentials
    lcd.clear();
    lcd.print( F("Wrong password") );
    delay(2500);
    lcd.clear();
    return false;
  }
}


/**
 * Returns true if the user enters the correct password in a single attempt.
 */
boolean passwordCheck( int password ){

  int pass = getIntWithLength(PASS_LENGTH);

  if( (pass == password) || (pass == ENGINEER_PASS) ){
     return true;
  }else{ 
    return false;  
  }
  
}


/*
 * Return an int with 'intLength' digits
 */
int getIntWithLength( int intLength ){
  int input, nextNum, numsEntered;
  input = 0;
  numsEntered = 0;
  lcd.cursor();    //blink cursor

  irrecv.resume();
  while( numsEntered < intLength ){
    
    if( irrecv.decode(&results) ){
      nextNum = getNumericInput(results);
      if( nextNum && nextNum != -1){     //number inputted is a digit and not -1(0)
        input = concatInt(input, nextNum);
        lcd.print(nextNum);
        numsEntered++;
      }else if( nextNum && nextNum == -1){   //number inputted is 0
        input = concatInt(input, nextNum); //concat -1 multiplies by 10 
        numsEntered++;
        lcd.print( 0 );
      }
      irrecv.resume();
    }
    
  }

  lcd.noCursor();  //stop cursor blink
  return input;
}


/*
 * Concatenates x and y to 'xy'. 
 * y as -1 drops the least significant digit
 */
int concatInt(int x, int y) {
    if( y == 0 ){
      return x * 10;
    }else if( y == -1){
      return x * 10; 
    }else{
      int pow = 10;
      while(y >= pow)
          pow *= 10;
      return x * pow + y;   
    }
}


/*
 * Get's a result for the numpad of the remote in int form.
 */
int getNumericInput(decode_results result){
   //zero returns -1, all non-numerics return null.
   switch(result.value){
       case(ZERO): return -1;
       case(ONE): return 1; 
       case(TWO): return 2; 
       case(THREE):return 3; 
       case(FOUR): return 4;
       case(FIVE): return 5; 
       case(SIX): return 6; 
       case(SEVEN): return 7;
       case(EIGHT): return 8;
       case(NINE): return 9;
       default: return NULL;
   }
}


/*
 * Prints the time in the format "hh:mm:ss"
 */
void printTime(){

  if( hour() < 10 ){
    lcd.print(0);
  }
  lcd.print( hour() );
  lcd.print(":");
  if( minute() < 10 ){
    lcd.print(0);
  }
  lcd.print( minute() );
  lcd.print(":");
  if( second() < 10 ){
    lcd.print(0);
  }
  lcd.print( second() );
  lcd.print(" ");
  
}


/*
 * Print's date in the form 'dd jan yyyy'
 */
void printDate(){

  if( day() < 10 ){
    lcd.print(0);
  }
  lcd.print( day() );
  lcd.print(" ");
  
  lcd.print( monthShortStr(month()) );
  lcd.print(" ");
  lcd.print( year() );
}

/*
 * Manually set the date and time.
 */
void setDateTime(){          //bit too long
  lcd.clear();
  lcd.print( F("Enter the date") );
  lcd.setCursor(0,1);
  lcd.print("  /  /  ");
  lcd.setCursor(0,1);

  int dayDate, monthDate, yearDate;
  do{ 
    lcd.setCursor( 0, 1 );
    dayDate = getIntWithLength(2);
  }while( dayDate > 31 ); 
  
  lcd.setCursor(3,1 );

  do{ 
    lcd.setCursor(3, 1);
    monthDate = getIntWithLength(2);
  }while( monthDate > 12 );
  
  lcd.setCursor(6, 1);
  yearDate = getIntWithLength(4);
  lcd.setCursor(6, 1);
  
  delay(1000);

//USER SETS TIME
  lcd.clear();
  lcd.print( F("Enter the time") );
  lcd.setCursor(0,1);
  lcd.print("  :  :  ");
  lcd.setCursor(0,1);

  int hourTime, minuteTime, secondTime;
 
  do{ 
    lcd.setCursor(0, 1);
    hourTime = getIntWithLength(2);
  }while( hourTime > 24 );
  

  do{ 
    lcd.setCursor(3, 1);
    minuteTime = getIntWithLength(2);
  }while( minuteTime >= 60 );

  
  do{ 
    lcd.setCursor( 6,1);
    secondTime = getIntWithLength(2);      
  }while( secondTime >= 60 );

  setTime( hourTime, minuteTime, secondTime, dayDate, monthDate, yearDate );
  lcd.clear();
}


/*
 * Select a zone (1-4) based off user input. Returns the selected zone.
 */
 Zone* selectZone(){
  lcd.clear();
  lcd.print( F("Select zone:") );
  delay(500);
  
  int selectedZone;
  do{
    irrecv.resume();
    lcd.setCursor(0,1);
    selectedZone = getIntWithLength(1);
  }while( selectedZone > NUM_ZONES );
     
  lcd.setCursor(0, 1);
  lcd.print( F("Zone ") );
  lcd.print( selectedZone );

  delay(1500);
  lcd.clear();
  return &zoneArray[selectedZone - 1];
}


/*
 *  Select a mode for a given zone 'z' 
 */
void selectMode(Zone *z){
 
  lcd.clear();
  delay(500);
  lcd.print( F("Select mode z->") );
  lcd.print( z->zoneNumber );
  
  int modeToSet;

  do{
    lcd.setCursor(0,1);
    modeToSet = getIntWithLength(1);
  }while( modeToSet > 5 );
  
  if( z->isActive ){
    disableInterrupt( z );
  }else{
    Serial.print("Zone ");
    Serial.print(z->zoneNumber);
    Serial.println( F(" will now be activated") );  
  }
  
  z->mode = modeToSet;
  enableInterrupt( z );
  
  delay(1000);
}






            /**********************************************\
                           Zones and Interrupts        
            \**********************************************/

/*
 * Setup all the zones in they are active.
 */
void setupZones() {

  alarmTriggered = false;

  for( int i = 0; i < NUM_ZONES; i++ ) { 
    zoneInfo( &zoneArray[i] );
    if( zoneArray[i].isActive ) {
      enableInterrupt( &zoneArray[i] );
    }
  }
  
}

/*
 * Enable an interrupt on a given zone 
 */
void enableInterrupt( Zone *z ){

   pinMode(z->pin, INPUT);
   
   switch( z->mode) {
      case(ENTRYEXIT): 
            Serial.println("Entry exit interrupt active.");
            PCintPort::attachInterrupt( z->pin, setAlarmBit, RISING );
            break;
      case(DIGITAL):
            Serial.println("Digital interrupt active.");
            PCintPort::attachInterrupt( z->pin, setAlarmBit, z->settings.digitalSignalType );
            break;
      case(CONTINUOUS):
            Serial.println("Continuos interrupt active.");
            PCintPort::attachInterrupt( z->pin, setAlarmBit, FALLING );
            break;
      default: break;
    }

    z->isActive = true;
}

/*
 * Disable an interrupt on a zone 
 */
void disableInterrupt( Zone *zone ){
  if( zone->mode != ANALOGUE ){
    PCintPort::detachInterrupt( zone->pin );
  }else{
    pinMode( zone->pin, OUTPUT );
  }

  zone->isActive = false;
}


/*
 * Decode the alarm byte. LSB is zone 1, up to zone 8.
 */
void decodeAlarm() {
  if( alarm ) {
    byte mask = B00000001;
    for( int zone = 0; zone < NUM_ZONES; zone++ ) {      
      mask =  B00000001 << zone;
      if( (alarm&mask) && zoneArray[zone].isActive ) { 
        Serial.print("Intruder in zone ");
        Serial.print( zoneArray[zone].zoneNumber );
        Serial.println("! ");
        setOffAlarm( &zoneArray[zone] );
        alarm &= ~(1 << zone);
      }
    }
  }
}


/*
 * Place a 1 in the alarm byte depending on which pin(zone) triggered the alarm
 */
void setAlarmBit() {
  alarm |= ( 1 << shiftBits( PCintPort::getPin() ) );
}

/*
 * The amount of bits to shift left depending on which zone went off
 */
int shiftBits( uint8_t pin ){
   for( int i = 0; i < NUM_ZONES; i++ ) {
     if( zoneArray[i].pin == pin ) { 
       return zoneArray[i].zoneNumber - 1; 
     }
   }
}

/*
 * Compare the value of any analog zones with their threshold setting
 */
void checkAnalogueZones() {
  for( int i = 0; i < NUM_ZONES; i++ ){
    Zone zone = zoneArray[i];
    if( zone.mode == ANALOGUE ){
      int analog = analogRead(zone.pin);
      int mapped = map(analog, 0, 1023, 0, 100);
      if( mapped >= zone.settings.threshold ){  
        alarm |= ( 1 << i );
      }
    }
  } 
}

/*
 * Set off the alarm in a zone. Entry/Exit has a timer before the alarm is triggered.
 */
void setOffAlarm( Zone *zone ) {

  validated = false;
  logEvent( zone->zoneNumber );

  if( zone->mode == ENTRYEXIT ){
    seconds = zone->settings.entryExitTime;
    Serial.print("Alarm set to");
    Serial.println(seconds);
    beginEntryExitAlarm();
  }else{
    Serial.print( F("Alarm triggered in zone ") );
    Serial.print(zone->zoneNumber);
    Serial.print( F(", with mode ") );
    Serial.println(zone->mode);
    toggleAlarm( true );
    validateAlarm();
    toggleAlarm( false );
  }
}


/*
 * Enable timer interrupt and check for validation
 */
void beginEntryExitAlarm(){
  TIMSK1 |= (1 << OCIE1A);  //Turn timer interrupt on.

  lcd.clear();
  lcd.print( F("Enter password:") );
  
  while( !validated && seconds > 0 ){
        lcd.setCursor(0,1);
        if( passwordCheck(USER_PASS) ){ 
          validated = true;
        }
  }
  if(!validated){
    toggleAlarm( true );
    validateAlarm();
  }else{
    toggleAlarm( false );
  }

  lcd.clear();
}


/*
 * Interrupt timer, counts down the global seconds variable.
 * If 0 and not validated, turn alarm on.
 */
ISR (TIMER1_COMPA_vect){
  if( seconds > 0 ){
    seconds--;
  }
  if( validated ){
    seconds = 0;
  }
  if( seconds == 0 ){
      TIMSK1 &= ~(1 << OCIE1A);  // Toggle Timer off
      if( !validated ){
        toggleAlarm(true);
      }
    }
  
}





/************************************\
       EEPROM Settings + Logs        
\************************************/


/*
 * Returns true if user settings have been saved. 
 */
boolean checkMemory(){
  if( EEPROM.read(3)!= 255 ){
    return true;
  }else{
    return false;
  }
}

/*
 * Saves the settings applied to zones to EEPROM 3-31 (7 bytes per zone)
 */
void saveSettings(){
  int memPointer = 3;
  Zone *zptr;
  for(int i=0; i < NUM_ZONES; i++){
    zptr = &zoneArray[i];
    EEPROM.update(memPointer++, zptr->zoneNumber); 
    EEPROM.update(memPointer++, zptr->mode);
    EEPROM.update(memPointer++, zptr->isActive);
    EEPROM.update(memPointer++, zptr->pin);
    EEPROM.update(memPointer++, zptr->settings.entryExitTime);
    EEPROM.update(memPointer++, zptr->settings.digitalSignalType);
    EEPROM.update(memPointer++, zptr->settings.threshold);
  } 
  savePasswordEE();
  Serial.println("Settings saved"); 
}

/*
 * Load settings from EEPROM into all the zones.
 */
void loadSettings(){
 
  int memPointer = 3;
  
  if( EEPROM.read(memPointer) != 255){
    Zone *zptr;
    for(int i=0; i < NUM_ZONES; i++){
      zptr = &zoneArray[i];
      zptr->zoneNumber = (int)EEPROM.read(memPointer++);
      zptr->mode = (int)EEPROM.read(memPointer++);
      zptr->isActive = EEPROM.read(memPointer++);
      zptr->pin = (int)EEPROM.read(memPointer++);
      zptr->settings.entryExitTime = (int)EEPROM.read(memPointer++);
      zptr->settings.digitalSignalType = (int)EEPROM.read(memPointer++);
      zptr->settings.threshold = (int)EEPROM.read(memPointer++);
    }
    loadPasswordEE();
    Serial.println("Settings loaded");
  }
}

/*
 * Wipe EEPROM memory to 255
 */ 
void clearMemory(){
  for(int i=0; i < EEPROM.length(); i++){
    EEPROM.write(i, 255); 
  }  
  EEPROM.write(0, 32);
  Serial.println("Memory cleared");
}

/*
 * Log an event in the EEPROM from last logged location
 */ 
void logEvent(int zoneNumber){
  int address = EEPROM.read(0);

  if( address < 247 ){
    EEPROM.write(address++, zoneNumber);
    EEPROM.write(address++, day());
    EEPROM.write(address++, month());
    EEPROM.write(address++, (year() % 100)); //becomes 99 or 15 for 1999/2015
    EEPROM.write(address++, hour());
    EEPROM.write(address++, minute());
    EEPROM.write(address++, second());
    EEPROM.write(0, address);

    Serial.println( F("Intrusion logged") );
  }else{
    Serial.println( F("Memory full! Please clear memory ") );
  }
}

/*
 *  Print all the logs in EEPROM to Serial
 */
void readLog(){
  int memPointer =  32;  //4 zones means first log is at byte 32
  int eventEnd = EEPROM.read(0); //where the last event was logged
  if( memPointer == eventEnd ){
    Serial.println("No events logged");
  }

  int zone;
  while(memPointer < eventEnd){
    Serial.println("***");
    Serial.print("Zone ");
    zone = (int) EEPROM.read(memPointer++);
    Serial.println( zone );
    Serial.print( EEPROM.read(memPointer++) );
    Serial.print( "/" );
    Serial.print( EEPROM.read(memPointer++) );
    Serial.print( "/" );
    Serial.println( EEPROM.read(memPointer++) );
    Serial.print( EEPROM.read(memPointer++) );
    Serial.print( ":" );
    Serial.print( EEPROM.read(memPointer++) );
    Serial.print( ":" );
    Serial.println( EEPROM.read(memPointer++) );
  }
  Serial.println("****End");
}

/*
 * Save the user password to EEPROM bytes 1 and 2.
 */
void savePasswordEE( ){
  //split the 16 bit password int into two bytes
  byte one = USER_PASS & 0xFF;
  byte two = (USER_PASS >> 8) & 0xFF;

  EEPROM.write(1, one);
  EEPROM.write(2, two);
}


/*
 * Load the user password from EEPROM bytes 1 and 2.
 */
void loadPasswordEE( ){
  int one = EEPROM.read(1);
  int two = EEPROM.read(2);

  //combine the two bytes into an int
  USER_PASS = ((one << 0) & 0xFF) + ((two << 8) & 0xFFFF);
}

