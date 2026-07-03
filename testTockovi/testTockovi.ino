// Definisanje pinova na ESP32-S3
const int IN1 = 4;
const int IN2 = 5;
const int IN3 = 6;
const int IN4 = 7;

const int LED_KONTROLERA = 21;

void setup() {
  // Postavljanje svih upravljačkih pinova kao izlaznih
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(LED_KONTROLERA, OUTPUT);

  // UKLJUČUJEMO DIODU ODMAH NA POČETKU DA SVETLI SVE VREME
  digitalWrite(LED_KONTROLERA, HIGH);
}

void loop() {
    // 3. FAZA: Samo DESNI napred (Levi STOJE)
  // Leva strana stoji:
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  // Desna strana ide napred:
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  delay(2000);

    // 4. FAZA: Samo LEVI napred (Desni STOJE)
  // Leva strana ide napred:
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  // Desna strana stoji:
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  delay(2000);


  // 1. FAZA: Svi motori NAPRED
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  delay(2000); // Radi 2 sekunde

  // 5. FAZA: KOČNICA (Sve stoji 2 sekunde pre nego što krene krug ispočetka)
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  delay(2000);

  // 2. FAZA: Svi motori UNAZAD
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  delay(2000);


}