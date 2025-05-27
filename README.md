#TimeStrike

Reflex & Timing Game
Un joc simplu de reflexe si timing implementat pe microcontroller AVR (Arduino), cu display LCD si butoane pentru 4 jucatori.
Ce face jocul?
Jocul are doua moduri principale:
1. Modul Reflex

Single Player: Apasa butonul cat mai repede dupa ce apare semnalul (sunet + mesaj pe LCD)

Poti selecta dificultatea (Easy, Medium, Hard, Insane) cu limite de timp
Daca esti mai rapid decat limita, castigi!


Multiplayer: Primul care apasa butonul dupa semnal castiga
Anti-cheat: Daca apesi inainte de semnal, esti "trisor" si pierzi

2. Modul Timing

Incearca sa apesi butonul cat mai aproape de 11 secunde dupa start
Poti juca singur sau cu pana la 4 jucatori
Castiga cel cu cea mai mica diferenta fata de timpul tinta

Hardware necesar

Microcontroller: ATmega328P (Arduino Uno)
Display: LCD 16x2 cu interfata I2C (adresa 0x27)
Butoane: 4 butoane pentru jucatori (conectate la pinii digitali)
LEDs: 4 LEDs pentru fiecare jucator (se aprind cand apesi butonul)
Buzzer: Pentru efecte sonore
Conexiuni I2C: SDA la A4, SCL la A5

Cum functioneaza codul?

State Machine: Jocul foloseste o masina de stari pentru a naviga prin meniuri si gameplay
Timere: Timer0 pentru functia millis(), Timer1 pentru PWM la buzzer
I2C: Comunicatia cu LCD-ul se face prin I2C
Interrupts: Pentru numaratoarea de milisecunde
Anti-debounce: Delay-uri pentru a evita apasarile multiple accidentale

Navigare

Butonul 1: Navigheaza prin optiuni (schimba modul, dificultatea, etc.)
Butonul 2: Selecteaza/confirma optiunea curenta
Butoanele 1-4: Butoanele de joc pentru fiecare jucator

Features cool

Sunet cand dai GO (1500Hz)
LED-uri care se aprind cand apesi butonul
Mesaje in romana pe LCD
Random delay inainte de semnal (2-5 secunde)
Calculul timpului cu precizie de milisecunde
Detectare trisori daca apesi prea devreme

Perfect pentru concursuri de reflexe cu prietenii! 🎮
