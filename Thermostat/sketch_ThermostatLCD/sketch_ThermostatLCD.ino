/**
 * Exemple de code pour lire un unique capteur DS18B20 sur un bus 1-Wire.
 */

/* Dépendance pour le bus 1-Wire */
#include <OneWire.h>
#include <LiquidCrystal.h>

/* Broche du bus 1-Wire */
const byte BROCHE_ONEWIRE = 2;
/** Objet LiquidCrystal pour communication avec l'écran LCD */
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
byte onewire_addr[8];
const byte HEATRELAY_PIN = 11;
/* Code de retour de la fonction getTemperature() */
enum DS18B20_RCODES
{
  READ_OK,         // Lecture ok
  NO_SENSOR_FOUND, // Pas de capteur
  INVALID_ADDRESS, // Adresse reçue invalide
  INVALID_SENSOR,  // Capteur invalide (pas un DS18B20)
  ASK_OK
};
/** Énumération des boutons utilisables */
enum
{
  BUTTON_NONE,  /*!< Pas de bouton appuyé */
  BUTTON_UP,    /*!< Bouton UP (haut) */
  BUTTON_DOWN,  /*!< Bouton DOWN (bas) */
  BUTTON_LEFT,  /*!< Bouton LEFT (gauche) */
  BUTTON_RIGHT, /*!< Bouton RIGHT (droite) */
  BUTTON_SELECT /*!< Bouton SELECT */
};

enum IHM_STATES
{
  IHMSTATES_START,
  IHMSTATES_MEASURING,
  IHMSTATES_MODESELECTION,
  IHMSTATES_ENTERDATA, // saisie des paramètres de mode (température ou durée avant extinction)
};

const byte MODESNB = 5; // nb de modes
enum MODE
{
  MODE_SETTEMP = 0,            // mode de selection de la temp à atteindre
  MODE_SETMAXDURATION_MIN = 1, // mode de selection de la durée avant arrêt
  MODE_SETMAXDURATION_H = 2,   // mode de selection de la durée avant arrêt
  MODE_RUNNING = 3,            // en cours de mesure
  MODE_OFF = 4                 // mode arret (pas de reglage de température)
};

char CurrentIhmState;
char CurrentMode = MODE_RUNNING;
const unsigned long BUTTONCHECK_INTERVAL = 100;       // interval entre 2 checks d'état de bouton
const unsigned long BUTTONSELECT_INTERVAL = 800;      // interval entre 2 appui sur bouton select
const unsigned long READTEMP_INTERVAL = 1000;         // interval entre 2 prises de temp
const unsigned long REQUESTTEMP_SHIFT = 800;          //décallage entre demande de lecture envoyée au capteur oneWire et la lecture du registre pour lui laisse le temps de réagire
const unsigned long HEATSTATESWITCH_INTERVAL = 10000; // interval min entre 2 changement d'états du chauffage
bool onewireWaitingForRead = false;
bool thermostatActivated = false;
bool timerON = false;
bool heatState = false; // HIGH : resistance chauffante active / LOW: inactive
unsigned long currentmillis;
unsigned long timerstartmillis = 0;
unsigned long lasttestbutton = 0;
unsigned long latestreadtemp = 0;
unsigned long latestSwitchHeater = 0;
unsigned long lastSelectPush = 0;
unsigned int maxduration = 60; // durée avant extinction (en minutes)
unsigned int countdown = 60;
float current_temp = 0;
float target_temp = 0;
char buffstr[16], s_temp[8];
bool blinkheat = false;

char modesdisplay[MODESNB][14] = {
    "choix temp",
    "duree minutes",
    "duree heures",
    "thermostat ON",
    "switch OFF"};

/* Création de l'objet OneWire pour manipuler le bus 1-Wire */
OneWire ds(BROCHE_ONEWIRE);

char lcdLineBuf[16];
uint8_t i;
uint8_t _DisplayLCD(const char *message, uint8_t line = 0, uint8_t pos = 0, uint8_t size = 16, bool overwrite = true)
{
  if (size <= 16)
  {
    if (overwrite && pos > 0)
    {
      for (i = 0; i < pos; i++)
      {
        *(lcdLineBuf + i) = ' ';
      }
    }
    for (i = 0; i < size; i++)
    {
      *(lcdLineBuf + pos + i) = message[i];
    }
    for (i = pos + size; i < 16; i++)
    {
      *(lcdLineBuf + i) = ' ';
    }
    lcd.setCursor(0, line);
    lcd.print(lcdLineBuf);
    return 1;
  }
  return 0;
}

#define DISPLAYLCD_1(message) _DisplayLCD(message, 0, 0, strlen(message), false)
#define DISPLAYLCD_2(message, line) _DisplayLCD(message, line, 0, strlen(message), false)
#define DISPLAYLCD_3(message, line, pos) _DisplayLCD(message, line, pos, strlen(message), false)
#define DISPLAYLCD_OVERWRITE(message, line, pos) _DisplayLCD(message, line, pos, strlen(message), true)

#define GET_4TH_ARG(arg1, arg2, arg3, arg4, ...) arg4
#define DISPLAYLCD_MACRO_CHOOSER(...)    \
  GET_4TH_ARG(__VA_ARGS__, DISPLAYLCD_3, \
              DISPLAYLCD_2, DISPLAYLCD_1, )

#define DISPLAYLCD(...)                 \
  DISPLAYLCD_MACRO_CHOOSER(__VA_ARGS__) \
  (__VA_ARGS__)

byte requestTemperature(byte reset_search)
{
  Serial.println(F("demande de lecture"));
  // data[] : Données lues depuis le scratchpad
  // onewire_addr[] : Adresse du module 1-Wire détecté

  /* Reset le bus 1-Wire ci nécessaire (requis pour la lecture du premier capteur) */
  if (reset_search)
  {
    ds.reset_search();
  }

  /* Recherche le prochain capteur 1-Wire disponible */
  if (!ds.search(onewire_addr))
  {
    // Pas de capteur
    return NO_SENSOR_FOUND;
  }

  /* Vérifie que l'adresse a été correctement reçue */
  if (OneWire::crc8(onewire_addr, 7) != onewire_addr[7])
  {
    // Adresse invalide
    return INVALID_ADDRESS;
  }

  /* Vérifie qu'il s'agit bien d'un DS18B20 */
  if (onewire_addr[0] != 0x28)
  {
    // Mauvais type de capteur
    return INVALID_SENSOR;
  }

  /* Reset le bus 1-Wire et sélectionne le capteur */
  ds.reset();
  ds.select(onewire_addr);

  /* Lance une prise de mesure de température et attend la fin de la mesure */
  ds.write(0x44, 1);
  return ASK_OK;
}

byte ReadTemperature(float *temperature)
{
  /* Reset le bus 1-Wire, sélectionne le capteur et envoie une demande de lecture du scratchpad */
  Serial.println(F("lecture"));
  byte data[9];
  ds.reset();
  ds.select(onewire_addr);
  ds.write(0xBE);

  /* Lecture du scratchpad */
  for (byte i = 0; i < 9; i++)
  {
    data[i] = ds.read();
  }

  /* Calcul de la température en degré Celsius */
  *temperature = (int16_t)((data[1] << 8) | data[0]) * 0.0625;

  // Pas d'erreur
  return READ_OK;
}

/** Retourne le bouton appuyé (si il y en a un) */
byte getPressedButton()
{

  /* Lit l'état des boutons */
  int value = analogRead(A0);

  /* Calcul l'état des boutons */
  if (value < 50)
    return BUTTON_RIGHT;
  else if (value < 250)
    return BUTTON_UP;
  else if (value < 450)
    return BUTTON_DOWN;
  else if (value < 650)
    return BUTTON_LEFT;
  else if (value < 850)
    return BUTTON_SELECT;
  else
    return BUTTON_NONE;
}

void Buttons_statemachin()
{
  byte buttonpressed = getPressedButton();
  if (buttonpressed == BUTTON_NONE)
  {
    return;
  }

  //gère la tempo par boutons (uniquement pour le select et right -qui a role de select-)
  // et la sortie du mode veille
  switch (buttonpressed)
  {
  case BUTTON_SELECT:
  case BUTTON_RIGHT:
    if (currentmillis - lastSelectPush < BUTTONSELECT_INTERVAL)
      return;
    else
    {
      lastSelectPush = currentmillis;
      if (CurrentMode == MODE_OFF)
        SwitchOn();
    }
    break;
  }
  // Serial.print("Button ");
  // Serial.print(buttonpressed);

  switch (CurrentIhmState)
  {
  case IHMSTATES_MEASURING:
  case IHMSTATES_START:
    if (buttonpressed == BUTTON_SELECT)
    {
      CurrentIhmState = IHMSTATES_MODESELECTION;
    }
    else
      return;
    break;

  case IHMSTATES_MODESELECTION:
    switch (buttonpressed)
    {
    case BUTTON_UP:
      if (++CurrentMode >= MODESNB)
        CurrentMode = 0;
      delay(200);
      break;
    case BUTTON_DOWN:
      if (--CurrentMode < 0)
        CurrentMode = MODESNB - 1;
      delay(200);
      break;
    case BUTTON_LEFT: // back button
      CurrentMode = MODE_RUNNING;
      CurrentIhmState = IHMSTATES_MEASURING;
      break;
    case BUTTON_SELECT:
    case BUTTON_RIGHT:
      switch (CurrentMode)
      {
      case MODE_RUNNING:
        CurrentIhmState = IHMSTATES_MEASURING;
        break;
      case MODE_SETTEMP:
        if (target_temp == 0)
          target_temp = floor(current_temp);
        CurrentIhmState = IHMSTATES_ENTERDATA;
        break;
      case MODE_SETMAXDURATION_H:
      case MODE_SETMAXDURATION_MIN:
        CurrentIhmState = IHMSTATES_ENTERDATA;
        break;
      case MODE_OFF:
        SwitchOff();
        break;
      default:
        break;
      }
      break;
    default:
      return;
    }
    break;

  case IHMSTATES_ENTERDATA:
    switch (buttonpressed)
    {
    case BUTTON_UP:
      switch (CurrentMode)
      {
      case MODE_SETTEMP:
        target_temp++;
        break;
      case MODE_SETMAXDURATION_MIN:
        if (maxduration < 1440)
          maxduration++;
        break;
      case MODE_SETMAXDURATION_H:
        if (maxduration < 1440)
          maxduration = maxduration + 60;
        break;
      }
      break;
    case BUTTON_DOWN:
      switch (CurrentMode)
      {
      case MODE_SETTEMP:
        target_temp--;
        break;
      case MODE_SETMAXDURATION_MIN:
        if (maxduration > 0)
          maxduration--;
        break;
      case MODE_SETMAXDURATION_H:
        if (maxduration >= 60)
          maxduration = maxduration - 60;
        break;
      }
      break;
    case BUTTON_SELECT:
      switch (CurrentMode)
      {
      case MODE_SETTEMP:
        thermostatActivated = true;
        break;
      case MODE_SETMAXDURATION_MIN:
      case MODE_SETMAXDURATION_H:
        timerON = true;
        timerstartmillis = currentmillis;
        break;
      }
      CurrentIhmState = IHMSTATES_MEASURING;
      CurrentMode = MODE_RUNNING;
      break;
    case BUTTON_LEFT: // back button
      CurrentIhmState = IHMSTATES_MODESELECTION;
      break;
    default:
      return;
    }
    break;
  }
  ManageDisplay();
  Serial.println("..OK");
}

void SwitchOff()
{
  CurrentMode = MODE_OFF;
  HeatSwitch(false);
  lcd.clear();
  lcd.noDisplay();
  digitalWrite(10, LOW); //extinction LCD light
}

void SwitchOn()
{
  CurrentMode = MODE_RUNNING;
  digitalWrite(10, HIGH); //allumage LCD light
  lcd.display();
}

void HeatSwitch(bool state)
{
  if (state != heatState)
  {
    heatState = state;
    digitalWrite(HEATRELAY_PIN, heatState);

    if (heatState)
      strcpy(s_temp, "ON");
    else
      strcpy(s_temp, "OFF");

    sprintf(buffstr, "set heat %s", s_temp);
    lcd.clear();
    DISPLAYLCD(buffstr, 0, 0);
    delay(1000);
  }
}

void ManageHeating()
{
  if (thermostatActivated && (currentmillis - latestSwitchHeater) > HEATSTATESWITCH_INTERVAL)
  {
    latestSwitchHeater = currentmillis;
    if (current_temp < target_temp)
      HeatSwitch(true);
    else
      HeatSwitch(false);
  }
}

void ManageDisplay()
{
  switch (CurrentIhmState)
  {
  case IHMSTATES_MEASURING:
    lcd.clear();
    if (timerON)
    {
      countdown = maxduration - (int)floor((currentmillis - timerstartmillis) / 60000);
      if (countdown <= 0)
        SwitchOff();
      dtostrf(countdown, 4, 0, s_temp);
      sprintf(buffstr, "%smin", s_temp);
      DISPLAYLCD(buffstr, 0, 0);
    }
    if (thermostatActivated)
    {
      dtostrf(target_temp, 2, 0, s_temp);
      sprintf(buffstr, "th<%sc", s_temp);
      DISPLAYLCD(buffstr, 0, 10);
    }

    dtostrf(current_temp, 5, 2, s_temp);
    sprintf(buffstr, "Temp:%sc", s_temp);
    DISPLAYLCD(buffstr, 1, 0);
    blinkheat = !blinkheat;
    if (heatState == HIGH && blinkheat)
    {
      DISPLAYLCD("HEAT", 1, 12);
    }
    break;
  case IHMSTATES_MODESELECTION:
    DISPLAYLCD("Mode:", 0, 0);
    DISPLAYLCD(modesdisplay[CurrentMode], 1, 0);
    break;
  case IHMSTATES_ENTERDATA:
    switch (CurrentMode)
    {
    case MODE_SETTEMP:
      DISPLAYLCD("reglage Temp:", 0, 0);
      dtostrf(target_temp, 5, 0, s_temp);
      sprintf(buffstr, "%sc", s_temp);
      DISPLAYLCD(buffstr, 1, 0);
      break;
    case MODE_SETMAXDURATION_MIN:
    case MODE_SETMAXDURATION_H:
      DISPLAYLCD("duree minutes", 0, 0);
      dtostrf(maxduration, 4, 0, s_temp);
      sprintf(buffstr, "%smin", s_temp);
      DISPLAYLCD(buffstr, 1, 0);
      break;
    default:
      return;
    }
    break;
  }
}

/** Fonction setup() **/
void setup()
{
  /* Initialisation du port série */
  Serial.begin(115200);
  lcd.begin(16, 2);
  DISPLAYLCD("Bienvenue");
  delay(500);
  DISPLAYLCD("sur", 1);
  delay(300);
  DISPLAYLCD_OVERWRITE("mon", 0, 0);
  delay(100);
  DISPLAYLCD_OVERWRITE("mon", 0, 3);
  delay(100);
  DISPLAYLCD_OVERWRITE("mon", 0, 6);
  delay(100);
  DISPLAYLCD_OVERWRITE("mon", 0, 9);
  delay(100);
  DISPLAYLCD_OVERWRITE("Thermostat", 1, 2);
  delay(1000);
  lasttestbutton = millis();
  pinMode(HEATRELAY_PIN, OUTPUT);
  pinMode(10, OUTPUT);
  CurrentIhmState = IHMSTATES_MEASURING;
  CurrentMode = MODE_RUNNING;
}

/** Fonction loop() **/
void loop()
{
  currentmillis = millis();

  if ((currentmillis - lasttestbutton) > BUTTONCHECK_INTERVAL)
  {
    lasttestbutton = currentmillis;
    Buttons_statemachin();
  }
  if (CurrentMode != MODE_OFF)
  {
    //inscript la demande de lecture
    if (CurrentIhmState == IHMSTATES_MEASURING &&
        !onewireWaitingForRead &&
        (currentmillis - latestreadtemp) > READTEMP_INTERVAL)
    {
      latestreadtemp = currentmillis;
      if (requestTemperature(true) != ASK_OK)
      {
        DISPLAYLCD("sensor ERROR");
        Serial.println(F("Erreur de demande de lecture du capteur"));
        return;
      }
      onewireWaitingForRead = true;
    }

    if (CurrentIhmState == IHMSTATES_MEASURING &&
        onewireWaitingForRead &&
        (currentmillis - latestreadtemp) > REQUESTTEMP_SHIFT)
    {
      onewireWaitingForRead = false;
      if (ReadTemperature(&current_temp) != READ_OK)
      {
        DISPLAYLCD("reading temp ERR");
        Serial.println(F("Erreur de lecture du capteur"));
        return;
      }
      Serial.print(F("Temperature : "));
      Serial.print(current_temp, 2);
      Serial.write(176); // Caractère degré
      Serial.write('C');
      Serial.println();

      ManageDisplay();

      ManageHeating();
    }
  }
}
