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
const byte MODESNB = 3;
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
  START,
  MEASURING,
  MODESELECTION,
  ENTERDATA, // saisie des paramètres de mode (température ou durée avant extinction)
};

enum MODE
{
  SETTEMP = 0,        // mode de selection de la temp à atteindre
  SETMAXDURATION = 1, // mode de selection de la durée avant arrêt
  RUNNING = 2         // en cours de mesure
};

byte CurrentIhmState;
int CurrentMode = RUNNING;
const unsigned long BUTTONCHECK_INTERVAL = 200;       // interval entre 2 checks d'état de bouton
const unsigned long READTEMP_INTERVAL = 1000;         // interval entre 2 prises de temp
const unsigned long REQUESTTEMP_SHIFT = 800;          //décallage entre demande de lecture envoyée au capteur oneWire et la lecture du registre pour lui laisse le temps de réagire
const unsigned long HEATSTATESWITCH_INTERVAL = 10000; // interval min entre 2 changement d'états du chauffage
bool onewireWaitingForRead = false;
bool thermostatActivated = false;
byte heatState = LOW; // HIGH : resistance chauffante active / LOW: inactive
unsigned long lasttestbutton = 0;
unsigned long latestreadtemp = 0;
unsigned long latestSwitchHeater = 0;
unsigned long maxduration = 60; // durée de chauffage max (en minutes)
unsigned long currentmillis;
float current_temp;
float target_temp = 0;

char modesdisplay[MODESNB][16] = {
    "choix temp     ",
    "duree heures   ",
    "thermostat ON  "};

/* Création de l'objet OneWire pour manipuler le bus 1-Wire */
OneWire ds(BROCHE_ONEWIRE);

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

void testButton()
{
  byte buttonpressed = getPressedButton();
  if (buttonpressed == BUTTON_NONE)
  {
    return;
  }

  switch (CurrentIhmState)
  {

  case START:
  case MEASURING:
    if (buttonpressed == BUTTON_SELECT)
      CurrentIhmState = MODESELECTION;
    else
      return;
    break;

  case MODESELECTION:
    switch (buttonpressed)
    {
    case BUTTON_UP:
      if (++CurrentMode >= MODESNB)
        CurrentMode = SETTEMP;
      break;
    case BUTTON_DOWN:
      if (--CurrentMode < 0)
        CurrentMode = RUNNING;
      break;
    case BUTTON_SELECT:
      CurrentIhmState = ENTERDATA;
      break;
    default:
      return;
    }
    break;

  case ENTERDATA:
    switch (buttonpressed)
    {
    case BUTTON_UP:
      switch (CurrentMode)
      {
      case SETTEMP:
        target_temp += 1;
        break;
      case SETMAXDURATION:
        maxduration += 1;
        break;
      }
      break;
    case BUTTON_DOWN:
      switch (CurrentMode)
      {
      case SETTEMP:
        target_temp -= 1;
        break;
      case SETMAXDURATION:
        maxduration -= 1;
        break;
      }
      break;
    case BUTTON_SELECT:
      CurrentIhmState = MEASURING;
      thermostatActivated = true;
      break;
    default:
      return;
    }
    break;
  }
  ManageDisplay();
}

void ManageHeating()
{
  if (thermostatActivated && (currentmillis - latestSwitchHeater) > HEATSTATESWITCH_INTERVAL)
  {
    latestSwitchHeater = currentmillis;
    if (current_temp < target_temp - 1)
    {
      heatState = HIGH;
      Serial.println(F("set heat ON"));
    }
    else
    {
      heatState = LOW;
      Serial.println(F("set heat OFF"));
    }
    digitalWrite(HEATRELAY_PIN, heatState);
  }
}

void ManageDisplay()
{
  switch (CurrentIhmState)
  {
  case MEASURING:
    char s_temp[5];
    dtostrf(current_temp, 5, 2, s_temp);
    lcd.setCursor(0, 0);
    lcd.print("Temp:           ");
    if (thermostatActivated)
    {
      lcd.setCursor(10, 0);
      lcd.print("<");
      lcd.setCursor(11, 0);
      char s_temptarget[2];
      dtostrf(target_temp, 2, 0, s_temptarget);
      lcd.print(s_temptarget);
    }
    lcd.setCursor(0, 1);
    lcd.print(s_temp);
    lcd.setCursor(5, 1);
    lcd.print("           ");

    break;
  case MODESELECTION:
    lcd.setCursor(0, 0);
    lcd.print("Mode:           ");
    lcd.setCursor(0, 1);
    lcd.print(modesdisplay[CurrentMode]);
    break;
  case ENTERDATA:
    switch (CurrentMode)
    {
    case SETTEMP:
      lcd.setCursor(0, 0);
      lcd.print("reglage Temp:");
      if (target_temp == 0)
        target_temp = current_temp;
      lcd.setCursor(0, 1);
      dtostrf(target_temp, 5, 2, s_temp);
      lcd.print(s_temp);
      lcd.setCursor(4, 1);
      lcd.print("            ");
      break;
    case SETMAXDURATION:
      lcd.print("duree minutes");
      char s_maxdur[4];
      dtostrf(maxduration, 4, 0, s_maxdur);
      lcd.setCursor(0, 1);
      lcd.print(s_maxdur);
      lcd.setCursor(4, 1);
      lcd.print("            ");
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
  CurrentIhmState = START;
  /* Initialisation du port série */
  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.print("Hello San:");
  lasttestbutton = millis();
  pinMode(HEATRELAY_PIN, OUTPUT);
}

/** Fonction loop() **/
void loop()
{
  currentmillis = millis();

  if ((currentmillis - lasttestbutton) > BUTTONCHECK_INTERVAL)
  {
    lasttestbutton = currentmillis;
    testButton();
  }

  //inscript la demande de lecture
  if (CurrentIhmState == MEASURING && 
  !onewireWaitingForRead && 
  (currentmillis - latestreadtemp) > READTEMP_INTERVAL)
  {
    latestreadtemp = currentmillis;
    if (requestTemperature(true) != ASK_OK)
    {
      Serial.println(F("Erreur de demande de lecture du capteur"));
      return;
    }
    onewireWaitingForRead = true;
  }

  if (CurrentIhmState == MEASURING && 
    onewireWaitingForRead && 
    (currentmillis - latestreadtemp) > REQUESTTEMP_SHIFT)
  {
    onewireWaitingForRead = false;
    if (ReadTemperature(&current_temp) != READ_OK)
    {
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
