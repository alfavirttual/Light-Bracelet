//#include <avr/iotn13a.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

// Разблокировка - два коротких нажатия и одно длинное 
// Режим показа уровня заряда батаери - Длинное, короткое, длинное, короткое нажатия 

unsigned char c = 0; /* биты
                          0
                          1 - счётчик нажатий 
                          2

                          3 - разблокировка

                          4 - синал о разблокировке  
                          
                          5 - выбор режима работы 
                          6

                          7 - сигнализирует о том, что режим работы выбран 
                         */
unsigned char h = 0; // счетчик нажатий и время удержания
unsigned char cnt = 0;

unsigned char &count = cnt; 
unsigned char &config = c; 
unsigned char &hold_tap = h;

#define LED_LIGHT PB2 // Пин к которому подключен основной светодиод
#define LED_GREEN PB0 // Зелёный светодиод 3й по счёту 
#define LED_YELLOW PB3 // Желтый светодиод центральный
#define LED_RED PB4 // Красный светодиод 1й по счёту
#define BUTTON_PIN PB1 // Пин к которому подключена кнопка (в Attiny13 только INT0)
#define ADC_PIN PB5 // Пин(АЦП) к которому подключена батарея 

#define MENU_FLASH_TIME 225 // Настройка периода мигания светодиодов при выборе режима
#define MENU_FLASH_TIME_HALF (MENU_FLASH_TIME + (256 - MENU_FLASH_TIME) / 2)
#define HOLD_TOUTCH_TIME 156 // Время удержания кнопки при котором нажатие считается длинным
#define STROB_TIME 100 // Период импульсов в реиме стробоскопа
#define SOS_IMPULSE 100 // Длительность длинный вспышек (короткие в 2 раза медленнее)
#define SOS_PAUSE 100 // Длительность пауз между вспышками 
#define LOW_BATTERY_IMPULSE_NUM 8 // Число импульсов, сигнализирующих о низком заряде батаери 
#define LOW_BATTTER_IMPULSE_TIME 100 // Длительность импульсов сигнализирующих о ником заряде батареи 

#define BUTTON_PRESS_STATE LOW   // Уровень сигнала на входе INT0 при замкнутой кнопке
#if (BUTTON_PRESS_STATE == LOW)
#define BUTTON_STATE (!(PINB & (1 << BUTTON_PIN)))
#elif (BUTTON_PRESS_STATE == HIGH)
#define BUTTON_STATE ((PINB & (1 << BUTTON_PIN)))
#endif

void wdc_enable(){
  SREG &= ~(1 << 7); // Запрещаю глобальные прерывания
  WDTCR |= (1 << WDCE) | (1 << WDE); 
  WDTCR = (1 << WDTIE) | (1 << WDP0) | (1 << WDP2);
  SREG |= (1 << 7); // Разрешаю глобальные прерывания
}

void wdc_disable(){
  SREG &= ~(1 << 7); // Запрещаю глобальные прерывания
  MCUSR &= ~(1 << WDRF);
  WDTCR |= (1 << WDCE) | (1 << WDE);
  WDTCR = 0x00;
  SREG |= (1 << 7); // Разрешаю глобальные прерывания
}


ISR(INT0_vect){
  if (config & 0b00010000){ // Если разблокированно 
  
    if(BUTTON_STATE){ // Кнопка нажата
      TCNT0 = 254; // Настройка таймера на срабатывание через 0.02 секунды
      TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024
      if (config > 128) { // Если установлен режим
          TCCR0B &= ~(1 << CS02) & ~(1 << CS00);
          PORTB &= ~(1 << LED_RED) & ~(1 << LED_YELLOW) & ~(1 << LED_GREEN) & ~(1 << LED_LIGHT);
          hold_tap = 0;
          config = 0;
          ADCSRA |= (1 << ADSC); // Запуск АЦП 
    }}
    else { // Если кнопка отжата 
        
      if (config < 128){
        if ((config & 0b01110000) != 80) config += 32;
        else config = 0b00010000;
        hold_tap = 0;
      }
    }
 
  } 
  else { // Если заблокированно
  
    if (BUTTON_STATE){ // кнопка нажата 

      TCNT0 = HOLD_TOUTCH_TIME; // Настройка таймера на срабатывание через 0.8 секунды
      TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024
      
    config ++; // Для разблокировки
    hold_tap ++; // Для показа уровня заряда 
    
    }
    else { // кнопка отпущена
      
      TCCR0B &= ~(1 << CS02) & ~(1 << CS00); // Останавливаем таймер
      PORTB &= ~(1 << LED_RED); // Если таймер не успел досчитать, а светодиод горел 

      switch (config & 0b00001111) 
      {
      case 0b00001111: // Если произошла разблокировка
        config = 0b00010111;
        hold_tap = 0;
        TCNT0 = 254; // Настройка таймера на срабатывание через 0.02 секунды
        TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024
        break;
      case 0b00000111: // Обнуляем счётчик нажатий при переполнении 
        config = 0;
        break;
      default:
        if((hold_tap & 0b10000000)) {// Смотрим входит ли в период длинного нажатия 
          config = 0b00000000;
        }
        if (config != 0) config ++;
        break;
      }

    
    if (hold_tap != 0) hold_tap ++;
    if (!((hold_tap == 0b10010010) || (hold_tap == 0b10100110)
       || (hold_tap == 0b00010100) || (hold_tap == 0b00101000))) {// обнуление переменной h_t при не правильной последовательноси нажатий 
      hold_tap = 0b00000000;
    }
    
    
    if (hold_tap == 0b00101000) {
      ADCSRA |= (1 << ADSC); // Запуск АЦП
    }
    if(hold_tap & 0b10000000) hold_tap &= 0b01111111;
    }
}
}

ISR(TIM0_OVF_vect){
    
    if (config & 0b00010000){ // Если разблокированно 
      if((config < 128)){ // Если режим не выбран
        TCNT0 = MENU_FLASH_TIME; // переход по вектору прерывания через 0.25 секунды
        OCR0A = MENU_FLASH_TIME_HALF; // переход по вектору прерывания через 0.2 секунды
        if (BUTTON_STATE) hold_tap ++;
        TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024
      }
      
      switch((config & 0b01110000)){
        case 16: // Если выбран режим 0 
          if (hold_tap < 4) PORTB |= (1 << LED_GREEN); // Удерживается менее 0.75 секунд
            else {
              TCCR0B &= ~(1 << CS02) & ~(1 << CS00);
              hold_tap = 0;
              config = 0b10010000;
              PORTB |= (1 << LED_LIGHT);
            }
        break;

        case 48: // стробоскоп
        if (hold_tap < 4) PORTB |= (1 << LED_YELLOW);
          else {
            PORTB |= (1 << LED_LIGHT);
            TCNT0 = 231; // Переход по вектору прерывания через 0.2 секунды
            OCR0A = 243; // Переъод по вектору прерывния через 0.1 секунды (период стробоскопа)
            config = 0b10110000;
            }    
          TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024  
          break;

        case 80:  // SOS
          if (hold_tap < 4) PORTB |= (1 << LED_RED);
            else {
              config = 0b11010000;
              PORTB |= (1 << LED_LIGHT);
              if (hold_tap <= 6){
                hold_tap ++;
                TCNT0 = 181; // Срабатывание через 0.6 секунды 
                OCR0A = 205; // Срабатывание через 0.2 секунды
              }
              else if(hold_tap <= 9){
                hold_tap ++;
                TCNT0 = 156; // Срабатывание через 0.8 секунды
                OCR0A = 206; // Срабатывание через 0.4 секунды 
              }
              else if(hold_tap == 10) {
                hold_tap = 4;
                TCNT0 = 254;
              }
            }
          TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024
          break;
        }
    }
  else { // Если заблокированно
  PORTB |= (1 << LED_RED);
  TCNT0 = HOLD_TOUTCH_TIME; // Настройка таймера на срабатывание через 0.8 секунды
  OCR0A = HOLD_TOUTCH_TIME + 20; // Настройка прерывания для моргания на 100 миллисекунд TCNT0 + 12
  if (hold_tap == 1 || hold_tap == 0b00010101) hold_tap += 16;
  else hold_tap = 0; 
  if ((config & 0b00000111) != 5) config = 0;
  else config = 0b00001111; 
  TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024
  hold_tap |= 0b10000000;
  }

}

ISR(TIM0_COMPA_vect){
  PORTB &= ~(1 << LED_RED) & ~(1 << LED_YELLOW) & ~(1 << LED_GREEN);
  if (config > 175) PORTB &= ~(1 << LED_LIGHT); // Выключение светодиода для второго и третьего режима
}

ISR(TIM0_COMPB_vect){
  if(count < LOW_BATTERY_IMPULSE_NUM){
    PORTB ^= (1 << LED_RED);
    count ++;
    TCNT0 = LOW_BATTTER_IMPULSE_TIME; // 0.3c
    TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024
  }
  else{
    TCCR0B &= ~(1 << CS02) & ~(1 << CS00);
    TIFR0 = 0xFF;
    TIMSK0 = ~TIMSK0;
    GIFR = (1 << INTF0);
    GIMSK = (1 << INT0);
  }
}

ISR(WDT_vect){

}
  
 
ISR(ADC_vect){ // Зажигаем светодиоды в зависимости от напряжения 
  if (hold_tap == 0b00101000) {
    if(ADC < 875) PORTB |= (1 << LED_RED); 
    else if((ADC > 875) && (ADC < 931)) PORTB |= (1 << LED_RED) | (1 << LED_YELLOW);
    else PORTB |= (1 << LED_RED) | (1 << LED_YELLOW) | (1 << LED_GREEN); 
    hold_tap = 0;
    config = 0;
    }
  else{ // Предупреждение о ником уровне заряда 
    if(ADC < 875){
      count = 0;
      TIMSK0 = ~TIMSK0;
      GIMSK = 0; 
      TCNT0 = LOW_BATTTER_IMPULSE_TIME; // 0.3c
      PORTB &= ~(1 << LED_RED);
      TCCR0B |= (1 << CS02) | (1 << CS00); //Запуск таймера с делителм на N = 1024

    }
  }
}

void port_ini(){
  DDRB &= ~(1 << BUTTON_PIN); // Настраиваю пин INT0 на вход
  DDRB |= (1 << LED_GREEN) | (1 << LED_RED) | (1 << LED_YELLOW) | (1 << LED_LIGHT);
  #if (BUTTON_PRESS_STATE == LOW)
    PORTB |= (1 << BUTTON_PIN);
  #endif
}

void interrrupt_ini(){
  
  GIMSK |= (1 << INT0); // Разрешаю прерывания по INT0
  MCUCR |= (1 << ISC00); // Прерывание при смене логического уровня
  GIFR = (1 << INTF0); // Очищаем регистр флагов прерываний 
}

void timer_ini(){
// TCCR0A |= (0 << WGM01) | (0 << WGM00); // Режим сравнения с регистром OCR0A
  TIMSK0 |= (1 << OCIE0A) | (1 << TOIE0); // Включил прерывания по совпадению с OCR0A и по переполнению счётчика
  OCR0B = 254; // Для уведомления о низком уровне заряда 
}

void adc_ini(){
  ADMUX |= (1 << REFS0); // Сравнение с внутренним ИОН на 1.1 В
  ADMUX &= ~(1 << MUX0) & ~(1 << MUX1); // Выбор пина ADC ADC_PIN
  ADCSRA |= (1 << ADIE) | (1 << ADEN); // Разрешить прерывание по завершению преобразования АЦП и активирование АЦП
}


int main(){

  port_ini();
  interrrupt_ini();
  timer_ini();
  adc_ini();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Выбран режим глубокого сна 
  

  SREG |= (1 << 7); // Разрешаю глобальные прерывания

  while(1){

  }

  return 0;

}