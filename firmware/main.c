#include <stdint.h>

/*
 * STM32 AIR MOUSE FIRMWARE (bare-metal, STM32F103C8T6)
 *
 * Changes compared with the previous version are marked:
 *   ===== NEW =====      added code
 *   ===== CHANGED =====  modified code
 */

// ============================================================
// REGISTER DEFINITIONS
// ============================================================

#define RCC_APB2ENR    (*(volatile uint32_t *)0x40021018)
#define RCC_APB1ENR    (*(volatile uint32_t *)0x4002101C)

#define GPIOA_CRH      (*(volatile uint32_t *)0x40010804)

#define GPIOB_CRL      (*(volatile uint32_t *)0x40010C00)
#define GPIOB_CRH      (*(volatile uint32_t *)0x40010C04)
#define GPIOB_IDR      (*(volatile uint32_t *)0x40010C08)
#define GPIOB_ODR      (*(volatile uint32_t *)0x40010C0C)
#define GPIOB_BSRR     (*(volatile uint32_t *)0x40010C10)

#define GPIOC_CRH      (*(volatile uint32_t *)0x40011004)
#define GPIOC_BSRR     (*(volatile uint32_t *)0x40011010)

#define I2C1_CR1       (*(volatile uint32_t *)0x40005400)
#define I2C1_CR2       (*(volatile uint32_t *)0x40005404)
#define I2C1_DR        (*(volatile uint32_t *)0x40005410)
#define I2C1_SR1       (*(volatile uint32_t *)0x40005414)
#define I2C1_SR2       (*(volatile uint32_t *)0x40005418)
#define I2C1_CCR       (*(volatile uint32_t *)0x4000541C)
#define I2C1_TRISE     (*(volatile uint32_t *)0x40005420)

#define USART1_SR      (*(volatile uint32_t *)0x40013800)
#define USART1_DR      (*(volatile uint32_t *)0x40013804)
#define USART1_BRR     (*(volatile uint32_t *)0x40013808)
#define USART1_CR1     (*(volatile uint32_t *)0x4001380C)

// ============================================================
// SYSTICK
// ============================================================

#define SYST_CSR       (*(volatile uint32_t *)0xE000E010)
#define SYST_RVR       (*(volatile uint32_t *)0xE000E014)
#define SYST_CVR       (*(volatile uint32_t *)0xE000E018)

// ===== NEW ===== tuning constants

#define CAL_SAMPLES        1000   // samples averaged at startup
#define CAL_MAX_SPREAD     800    // counts; bigger = board was moving
#define CAL_MAX_ATTEMPTS   10     // give up retrying after this many
#define READ_FAIL_LIMIT    20     // consecutive I2C failures before halt


volatile uint32_t system_ms = 0;


void SysTick_Handler(void)
{
    system_ms++;
}


void SysTick_Init(void)
{
    // CPU clock = 8 MHz -> one interrupt every 1 ms.
    SYST_RVR = 7999;
    SYST_CVR = 0;
    SYST_CSR = (1 << 0) | (1 << 1) | (1 << 2);
}


// ===== NEW ===== wait using SysTick (call only after SysTick_Init)

void wait_ms(uint32_t ms)
{
    uint32_t start = system_ms;

    while ((system_ms - start) < ms)
    {
        // Wait.
    }
}


// ============================================================
// DELAYS
// ============================================================

void delay_short(void)
{
    for (volatile uint32_t i = 0; i < 10000; i++);
}


void delay_visible(void)
{
    for (volatile uint32_t i = 0; i < 500000; i++);
}


void delay_gap(void)
{
    for (volatile uint32_t i = 0; i < 1500000; i++);
}


// ============================================================
// LED
// ============================================================

void LED_ON(void)
{
    // PC13 LED is active LOW.
    GPIOC_BSRR = (1 << 29);
}


void LED_OFF(void)
{
    GPIOC_BSRR = (1 << 13);
}


void Blink_Error(uint8_t count)
{
    while (1)
    {
        for (uint8_t i = 0; i < count; i++)
        {
            LED_ON();
            delay_visible();

            LED_OFF();
            delay_visible();
        }

        delay_gap();
    }
}


// ============================================================
// UART
// ============================================================

void UART1_Init(void)
{
    // GPIOA clock
    RCC_APB2ENR |= (1 << 2);

    // USART1 clock
    RCC_APB2ENR |= (1 << 14);

    // PA9 = USART1 TX, alternate-function push-pull, 50 MHz
    GPIOA_CRH &= ~(0xF << 4);
    GPIOA_CRH |= (0xB << 4);

    // 8 MHz / 115200
    USART1_BRR = 0x45;

    // Transmitter enable
    USART1_CR1 |= (1 << 3);

    // USART enable
    USART1_CR1 |= (1 << 13);
}


void UART1_SendChar(char c)
{
    while (!(USART1_SR & (1 << 7)));

    USART1_DR = c;
}


void UART1_SendString(const char *text)
{
    while (*text)
    {
        UART1_SendChar(*text);
        text++;
    }
}


void UART1_SendInt(int16_t value)
{
    char buffer[8];
    uint8_t i = 0;

    int32_t number = value;

    if (number < 0)
    {
        UART1_SendChar('-');
        number = -number;
    }

    if (number == 0)
    {
        UART1_SendChar('0');
        return;
    }

    while (number > 0)
    {
        buffer[i] = (number % 10) + '0';
        number /= 10;
        i++;
    }

    while (i > 0)
    {
        i--;
        UART1_SendChar(buffer[i]);
    }
}


// ============================================================
// PB12 LEFT-CLICK BUTTON
// ============================================================
//
// PB12 ---- BUTTON ---- GND
// Internal pull-up: released = HIGH, pressed = LOW
//
// ============================================================

void Button_Init(void)
{
    // PB12 is in GPIOB_CRH (bits 16-19): MODE=00, CNF=10 (input pull-up/down)
    GPIOB_CRH &= ~(0xF << 16);
    GPIOB_CRH |= (0x8 << 16);

    // ODR = 1 selects the internal pull-UP.
    GPIOB_ODR |= (1 << 12);
}


uint8_t Button_IsPressed(void)
{
    if ((GPIOB_IDR & (1 << 12)) == 0)
    {
        return 1;
    }

    return 0;
}


// ===== NEW =====
// ============================================================
// PB13 RIGHT-CLICK BUTTON
// ============================================================
//
// PB13 ---- BUTTON ---- GND
// Internal pull-up: released = HIGH, pressed = LOW
//
// ============================================================

void Button2_Init(void)
{
    // PB13 is in GPIOB_CRH (bits 20-23): MODE=00, CNF=10 (input pull-up/down)
    GPIOB_CRH &= ~(0xF << 20);
    GPIOB_CRH |= (0x8 << 20);

    // ODR = 1 selects the internal pull-UP.
    GPIOB_ODR |= (1 << 13);
}


uint8_t Button2_IsPressed(void)
{
    if ((GPIOB_IDR & (1 << 13)) == 0)
    {
        return 1;
    }

    return 0;
}


// ============================================================
// I2C BUS RECOVERY
// ============================================================

void I2C_BusRecovery(void)
{
    I2C1_CR1 &= ~(1 << 0);

    // PB6/PB7 temporary GPIO open-drain
    GPIOB_CRL &= ~((0xF << 24) | (0xF << 28));
    GPIOB_CRL |= ((0x6 << 24) | (0x6 << 28));

    // Release SCL and SDA
    GPIOB_BSRR = (1 << 6);
    GPIOB_BSRR = (1 << 7);
    delay_short();

    // SDA LOW
    GPIOB_BSRR = (1 << 23);
    delay_short();

    // SCL LOW
    GPIOB_BSRR = (1 << 22);
    delay_short();

    // SCL HIGH
    GPIOB_BSRR = (1 << 6);
    delay_short();

    // SDA HIGH
    GPIOB_BSRR = (1 << 7);
    delay_short();

    // Restore PB6/PB7 to I2C alternate-function open-drain.
    GPIOB_CRL &= ~((0xF << 24) | (0xF << 28));
    GPIOB_CRL |= ((0xF << 24) | (0xF << 28));

    // Software reset of the I2C peripheral.
    I2C1_CR1 |= (1 << 15);
    delay_short();
    I2C1_CR1 &= ~(1 << 15);
}


// ===== NEW ===== I2C peripheral setup in one place (reused after recovery)

void I2C1_Init(void)
{
    // PCLK1 = 8 MHz
    I2C1_CR2 = 8;

    // 100 kHz I2C
    I2C1_CCR = 40;
    I2C1_TRISE = 9;

    // Enable I2C
    I2C1_CR1 |= 1;
}


// ===== NEW ===== recover a stuck bus and restart I2C

void I2C_RecoverAndRestart(void)
{
    I2C_BusRecovery();
    I2C1_Init();
}


// ============================================================
// I2C BASIC FUNCTIONS
// ============================================================

int I2C_Start(void)
{
    uint32_t timeout = 100000;

    I2C1_CR1 |= (1 << 8);

    while (!(I2C1_SR1 & (1 << 0)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    return 1;
}


int I2C_SendAddress_Write(uint8_t address)
{
    uint32_t timeout = 100000;

    I2C1_DR = address << 1;

    while (!(I2C1_SR1 & (1 << 1)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    volatile uint32_t temp;

    temp = I2C1_SR1;
    temp = I2C1_SR2;
    (void)temp;

    return 1;
}


int I2C_SendByte(uint8_t data)
{
    uint32_t timeout = 100000;

    while (!(I2C1_SR1 & (1 << 7)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    I2C1_DR = data;

    timeout = 100000;

    while (!(I2C1_SR1 & (1 << 2)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    return 1;
}


int I2C_SendAddress_Read(uint8_t address)
{
    uint32_t timeout = 100000;

    I2C1_DR = (address << 1) | 1;

    while (!(I2C1_SR1 & (1 << 1)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    // Disable ACK for one-byte receive.
    I2C1_CR1 &= ~(1 << 10);

    volatile uint32_t temp;

    temp = I2C1_SR1;
    temp = I2C1_SR2;
    (void)temp;

    // Generate STOP.
    I2C1_CR1 |= (1 << 9);

    return 1;
}


int I2C_ReadByte(uint8_t *value)
{
    uint32_t timeout = 100000;

    while (!(I2C1_SR1 & (1 << 6)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    *value = (uint8_t)I2C1_DR;

    return 1;
}


// ============================================================
// MPU SINGLE REGISTER READ
// ============================================================

int MPU_ReadRegister(uint8_t reg, uint8_t *value)
{
    uint32_t timeout = 100000;

    while (I2C1_SR2 & (1 << 1))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    if (!I2C_Start())                    return 0;
    if (!I2C_SendAddress_Write(0x68))    return 0;
    if (!I2C_SendByte(reg))              return 0;
    if (!I2C_Start())                    return 0;
    if (!I2C_SendAddress_Read(0x68))     return 0;
    if (!I2C_ReadByte(value))            return 0;

    return 1;
}


// ============================================================
// MPU WAKE-UP
// ============================================================

int MPU_WakeUp(void)
{
    uint32_t timeout = 100000;

    while (I2C1_SR2 & (1 << 1))
    {
        if (--timeout == 0)
        {
            return 1;
        }
    }

    if (!I2C_Start())                    return 2;
    if (!I2C_SendAddress_Write(0x68))    return 3;
    if (!I2C_SendByte(0x6B))             return 4;
    if (!I2C_SendByte(0x00))             return 5;

    I2C1_CR1 |= (1 << 9);

    return 0;
}


// ===== NEW =====
// ============================================================
// MPU SINGLE REGISTER WRITE
// ============================================================

int MPU_WriteRegister(uint8_t reg, uint8_t value)
{
    uint32_t timeout = 100000;

    while (I2C1_SR2 & (1 << 1))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    if (!I2C_Start())                    return 0;
    if (!I2C_SendAddress_Write(0x68))    return 0;
    if (!I2C_SendByte(reg))              return 0;
    if (!I2C_SendByte(value))            return 0;

    // STOP
    I2C1_CR1 |= (1 << 9);

    delay_short();

    return 1;
}


// ===== NEW =====
// ============================================================
// MPU CONFIGURATION
// ============================================================
//
// 0x6B PWR_MGMT_1   = 0x01 : clock from gyro PLL (more stable)
// 0x19 SMPLRT_DIV   = 9    : 1 kHz / (1 + 9) = 100 Hz
// 0x1A CONFIG       = 3    : digital low-pass filter ~42 Hz
// 0x1B GYRO_CONFIG  = 0x00 : +-250 deg/s (131 counts per deg/s)
//
// Returns 0 on success, otherwise the number of the failed step.
// ============================================================

uint8_t MPU_Configure(void)
{
    if (!MPU_WriteRegister(0x6B, 0x01)) return 1;

    // Let the new clock source settle.
    delay_visible();

    if (!MPU_WriteRegister(0x19, 9))    return 2;
    if (!MPU_WriteRegister(0x1A, 3))    return 3;
    if (!MPU_WriteRegister(0x1B, 0x00)) return 4;

    // Let the filter settle.
    delay_visible();

    return 0;
}


// ============================================================
// GYROSCOPE BURST READ
// ============================================================

int MPU_ReadGyroBurst(
    int16_t *gyro_x,
    int16_t *gyro_y,
    int16_t *gyro_z
)
{
    uint8_t data[6];

    uint8_t index = 0;
    uint8_t remaining = 6;

    uint32_t timeout;

    volatile uint32_t temp;


    timeout = 100000;

    while (I2C1_SR2 & (1 << 1))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    // POS = 0
    I2C1_CR1 &= ~(1 << 11);

    // ACK = 1
    I2C1_CR1 |= (1 << 10);

    if (!I2C_Start())                    return 0;
    if (!I2C_SendAddress_Write(0x68))    return 0;

    // Start at GYRO_XOUT_H = 0x43.
    if (!I2C_SendByte(0x43))             return 0;

    // Repeated START.
    if (!I2C_Start())                    return 0;

    // MPU address + READ.
    I2C1_DR = (0x68 << 1) | 1;

    timeout = 100000;

    while (!(I2C1_SR1 & (1 << 1)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    // Clear ADDR.
    temp = I2C1_SR1;
    temp = I2C1_SR2;
    (void)temp;

    // Read until only 3 bytes remain.
    while (remaining > 3)
    {
        timeout = 100000;

        while (!(I2C1_SR1 & (1 << 6)))
        {
            if (--timeout == 0)
            {
                return 0;
            }
        }

        data[index] = (uint8_t)I2C1_DR;

        index++;
        remaining--;
    }

    // STM32F1 last-three-byte sequence.
    timeout = 100000;

    while (!(I2C1_SR1 & (1 << 2)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    // Disable ACK.
    I2C1_CR1 &= ~(1 << 10);

    // Read N-2.
    data[index] = (uint8_t)I2C1_DR;
    index++;
    remaining--;

    // Generate STOP.
    I2C1_CR1 |= (1 << 9);

    // Read N-1.
    data[index] = (uint8_t)I2C1_DR;
    index++;
    remaining--;

    timeout = 100000;

    while (!(I2C1_SR1 & (1 << 6)))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    // Read final byte.
    data[index] = (uint8_t)I2C1_DR;
    index++;
    remaining--;

    timeout = 100000;

    while (I2C1_CR1 & (1 << 9))
    {
        if (--timeout == 0)
        {
            return 0;
        }
    }

    // Restore ACK.
    I2C1_CR1 |= (1 << 10);

    // Convert bytes into signed 16-bit values.
    *gyro_x = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    *gyro_y = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
    *gyro_z = (int16_t)(((uint16_t)data[4] << 8) | data[5]);

    return 1;
}


// ============================================================
// MAIN
// ============================================================

int main(void)
{
    // ========================================================
    // CLOCKS
    // ========================================================

    RCC_APB2ENR |= (1 << 3);     // GPIOB
    RCC_APB2ENR |= (1 << 4);     // GPIOC
    RCC_APB1ENR |= (1 << 21);    // I2C1


    // ========================================================
    // PC13 LED
    // ========================================================

    GPIOC_CRH &= ~(0xF << 20);
    GPIOC_CRH |= (0x2 << 20);

    LED_OFF();


    // ========================================================
    // PB12 BUTTON
    // ========================================================

    Button_Init();
    Button2_Init();    // ===== NEW =====


    // ========================================================
    // UART
    // ========================================================

    UART1_Init();

    UART1_SendString("\r\n\r\n============================\r\n");
    UART1_SendString("AirMouse 100 Hz + 2 Buttons v3\r\n");
    UART1_SendString("Firmware started.\r\n");
    UART1_SendString("============================\r\n");


    // ========================================================
    // I2C
    // ========================================================

    I2C_BusRecovery();

    // ===== CHANGED ===== uses I2C1_Init() (same settings as before)
    I2C1_Init();

    delay_visible();


    // ========================================================
    // WHO_AM_I
    // ========================================================

    uint8_t who_am_i = 0;

    UART1_SendString("Checking MPU...\r\n");

    if (!MPU_ReadRegister(0x75, &who_am_i))
    {
        UART1_SendString("WHO_AM_I READ ERROR!\r\n");

        Blink_Error(7);
    }

    UART1_SendString("WHO_AM_I=0x");

    if (who_am_i == 0x68)
    {
        UART1_SendString("68");
    }
    else if (who_am_i == 0x70)
    {
        UART1_SendString("70");
    }
    else
    {
        UART1_SendString("??");
    }

    UART1_SendString("\r\n");

    if (who_am_i != 0x68 && who_am_i != 0x70)
    {
        UART1_SendString("UNKNOWN MPU!\r\n");

        Blink_Error(8);
    }


    // ========================================================
    // WAKE MPU
    // ========================================================

    UART1_SendString("Waking MPU...\r\n");

    uint8_t wake_status = MPU_WakeUp();

    if (wake_status != 0)
    {
        UART1_SendString("MPU WAKE ERROR!\r\n");

        Blink_Error(wake_status);
    }

    UART1_SendString("MPU READY.\r\n");

    delay_visible();


    // ===== NEW =====
    // ========================================================
    // CONFIGURE MPU (clock, 100 Hz rate, low-pass filter, range)
    // ========================================================

    UART1_SendString("Configuring MPU...\r\n");

    uint8_t config_status = MPU_Configure();

    if (config_status != 0)
    {
        UART1_SendString("MPU CONFIG ERROR!\r\n");

        Blink_Error(9);
    }

    UART1_SendString("MPU CONFIGURED.\r\n");


    // ========================================================
    // START 1 ms TIMER
    // ========================================================

    SysTick_Init();


    // ========================================================
    // GYROSCOPE STARTUP CALIBRATION
    // ========================================================
    //
    // Keep the MPU completely still.
    //
    // ===== CHANGED =====
    // - 1000 samples instead of 500
    // - 1000 ms settle time instead of 500 ms
    // - if the board moved during calibration (large spread)
    //   it automatically tries again
    //
    // ========================================================

    int16_t calibration_x = 0;
    int16_t calibration_y = 0;
    int16_t calibration_z = 0;

    int16_t gyro_x_offset = 0;
    int16_t gyro_z_offset = 0;

    UART1_SendString("\r\nCALIBRATING...\r\n");
    UART1_SendString("Keep MPU completely still!\r\n");

    // Let the sensor settle.
    wait_ms(1000);

    for (int attempt = 0; attempt < CAL_MAX_ATTEMPTS; attempt++)
    {
        int32_t gyro_x_sum = 0;
        int32_t gyro_z_sum = 0;

        int16_t min_x =  32767;
        int16_t max_x = -32768;
        int16_t min_z =  32767;
        int16_t max_z = -32768;

        for (int i = 0; i < CAL_SAMPLES; i++)
        {
            if (!MPU_ReadGyroBurst(
                    &calibration_x,
                    &calibration_y,
                    &calibration_z))
            {
                UART1_SendString("CALIBRATION GYRO ERROR!\r\n");

                Blink_Error(12);
            }

            gyro_x_sum += calibration_x;
            gyro_z_sum += calibration_z;

            if (calibration_x < min_x) min_x = calibration_x;
            if (calibration_x > max_x) max_x = calibration_x;
            if (calibration_z < min_z) min_z = calibration_z;
            if (calibration_z > max_z) max_z = calibration_z;

            // Wait approximately 2 ms.
            wait_ms(2);
        }

        gyro_x_offset = (int16_t)(gyro_x_sum / CAL_SAMPLES);
        gyro_z_offset = (int16_t)(gyro_z_sum / CAL_SAMPLES);

        if (((int32_t)max_x - min_x) < CAL_MAX_SPREAD &&
            ((int32_t)max_z - min_z) < CAL_MAX_SPREAD)
        {
            // Board stayed still: calibration accepted.
            break;
        }

        UART1_SendString("MOVED! RETRYING...\r\n");

        wait_ms(500);
    }

    UART1_SendString("GX_OFFSET=");
    UART1_SendInt(gyro_x_offset);

    UART1_SendString(" GZ_OFFSET=");
    UART1_SendInt(gyro_z_offset);

    UART1_SendString("\r\n");
    UART1_SendString("CALIBRATION DONE\r\n");

    UART1_SendString("STARTING SENSOR READ LOOP...\r\n");


    // ========================================================
    // SENSOR VARIABLES
    // ========================================================

    int16_t gyro_x = 0;
    int16_t gyro_y = 0;
    int16_t gyro_z = 0;

    uint32_t last_sample_ms = system_ms;

    uint8_t button_state = 0;
    uint8_t button2_state = 0;   // ===== NEW =====

    uint32_t last_alive_ms = system_ms;

    // ===== NEW ===== counts consecutive I2C failures
    uint8_t read_failures = 0;


    // ========================================================
    // MAIN LOOP
    // ========================================================

    while (1)
    {
        // Heartbeat once per second.
        if ((system_ms - last_alive_ms) >= 1000)
        {
            last_alive_ms += 1000;

            UART1_SendString("LOOP ALIVE\r\n");
        }

        // One packet every 10 ms (100 Hz).
        if ((system_ms - last_sample_ms) >= 10)
        {
            last_sample_ms += 10;


            // =================================================
            // READ GYROSCOPE
            // =================================================

            if (!MPU_ReadGyroBurst(&gyro_x, &gyro_y, &gyro_z))
            {
                // ===== CHANGED =====
                // Instead of freezing forever on the first failure,
                // recover the I2C bus and try again. Only halt if
                // it keeps failing.

                read_failures++;

                if (read_failures >= READ_FAIL_LIMIT)
                {
                    UART1_SendString("GYRO READ ERROR!\r\n");

                    Blink_Error(11);
                }

                I2C_RecoverAndRestart();

                continue;
            }

            read_failures = 0;


            // =================================================
            // REMOVE CALIBRATION OFFSET
            // =================================================

            gyro_x -= gyro_x_offset;
            gyro_z -= gyro_z_offset;


            // =================================================
            // READ BUTTON
            // =================================================

            button_state = Button_IsPressed();
            button2_state = Button2_IsPressed();   // ===== NEW =====


            // =================================================
            // SEND DATA TO PC
            // =================================================

            UART1_SendString("GX=");
            UART1_SendInt(gyro_x);

            UART1_SendString(" GZ=");
            UART1_SendInt(gyro_z);

            UART1_SendString(" B=");
            UART1_SendInt(button_state);

            // ===== NEW ===== right-click button
            UART1_SendString(" R=");
            UART1_SendInt(button2_state);

            UART1_SendString("\r\n");
        }
    }
}