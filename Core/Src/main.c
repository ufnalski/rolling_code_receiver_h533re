/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fdcan.h"
#include "hash.h"
#include "i2c.h"
#include "icache.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ssd1306.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define TRANSMITTER_SERIAL_NUMBER 0xDEADC0DE // 4-byte
#define SECRET_KEY 0xDEADBEEF  // 4-byte
#define COUNTER_WINDOW_WIDTH 5  // max 255

// colors of terminal
// https://en.wikipedia.org/wiki/ANSI_escape_code
#define DEFAULT_TERMINAL "\e[0m"
#define RED_TERMINAL "\e[31m"
#define GREEN_TERMINAL "\e[32m"
#define MAGENTA_TERMINAL "\e[35m"
#define CYAN_TERMINAL "\e[36m"
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
__IO uint32_t BspButtonState = BUTTON_RELEASED;

/* USER CODE BEGIN PV */
FDCAN_RxHeaderTypeDef rxHeader;

union
{
	uint8_t Bytes[16];
	uint32_t Words[4];
} rxData; // CAN FD payload: 4-byte serial number, 11-byte hash of secret key appended with a counter, 1-byte action code

union
{
	uint8_t Bytes[12];
	uint32_t Words[3];
} dataToBeHashed;

uint8_t SHA256Digest[32];
uint8_t fob_message_received_flag = 0;

uint32_t rolling_counter_rx = 0;
uint8_t rolling_counter_advance = 0;
uint8_t is_hash_ok;

char lcd_line[32];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void PrintTimestamp();
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{

	/* USER CODE BEGIN 1 */

	/* USER CODE END 1 */

	/* MCU Configuration--------------------------------------------------------*/

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* USER CODE BEGIN Init */

	/* USER CODE END Init */

	/* Configure the system clock */
	SystemClock_Config();

	/* USER CODE BEGIN SysInit */

	/* USER CODE END SysInit */

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_ICACHE_Init();
	MX_FDCAN1_Init();
	MX_I2C3_Init();
	MX_HASH_Init();
	/* USER CODE BEGIN 2 */

	ssd1306_Init();
	ssd1306_Fill(Black);
	ssd1306_SetCursor(20, 0);
	ssd1306_WriteString("ufnalski.edu.pl", Font_6x8, White);
	ssd1306_SetCursor(20, 12);
	ssd1306_WriteString("Rolling code RX", Font_6x8, White);
	ssd1306_UpdateScreen();

	HAL_FDCAN_ConfigGlobalFilter(&hfdcan1, FDCAN_REJECT, FDCAN_REJECT,
	FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE);

	if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
	{
		Error_Handler();
	}

	if (HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE,
			0) != HAL_OK)
	{
		Error_Handler();
	}

	dataToBeHashed.Words[0] = SECRET_KEY;
	/* USER CODE END 2 */

	/* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
	BspCOMInit.BaudRate = 115200;
	BspCOMInit.WordLength = COM_WORDLENGTH_8B;
	BspCOMInit.StopBits = COM_STOPBITS_1;
	BspCOMInit.Parity = COM_PARITY_NONE;
	BspCOMInit.HwFlowCtl = COM_HWCONTROL_NONE;
	if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
	{
		Error_Handler();
	}

	/* USER CODE BEGIN BSP */

	/* -- Sample board code to send message over COM1 port ---- */
	printf(DEFAULT_TERMINAL"\r\n\r\n"DEFAULT_TERMINAL);
	PrintTimestamp();
	printf(
			DEFAULT_TERMINAL"Welcome to KeeLoq world of rolling/hopping codes!\r\n"DEFAULT_TERMINAL);
	HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin, GPIO_PIN_RESET);
	/* USER CODE END BSP */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1)
	{
		if (fob_message_received_flag == 1)
		{
			fob_message_received_flag = 0;

			sprintf(lcd_line, "RX: %08lX %08lX", rxData.Words[0],
					rxData.Words[1]);
			ssd1306_SetCursor(2, 36);
			ssd1306_WriteString(lcd_line, Font_6x8, White);
			sprintf(lcd_line, "   %08lX %02lX %06lX", rxData.Words[2],
					(rxData.Words[3] & 0xFF000000) >> 24,
					rxData.Words[3] & 0x00FFFFFF);
			ssd1306_SetCursor(2, 48);
			ssd1306_WriteString(lcd_line, Font_6x8, White);
			ssd1306_UpdateScreen();

			if (rxData.Words[0] == TRANSMITTER_SERIAL_NUMBER)
			{
				dataToBeHashed.Words[1] = rolling_counter_rx;
				HAL_HASH_Start(&hhash, dataToBeHashed.Bytes, 8, SHA256Digest,
						100);
				is_hash_ok = 1;
				for (uint8_t i = 0; i < 11; i++)
				{
					is_hash_ok &= (rxData.Bytes[i + 4] == SHA256Digest[i]);
				}

				if (is_hash_ok == 1)
				{
					PrintTimestamp();
					printf(
							GREEN_TERMINAL"Hash OK. (Counters in sync.) Action granted.\r\n"DEFAULT_TERMINAL);
					HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin,
							GPIO_PIN_SET);
					rolling_counter_rx++;
					sprintf(lcd_line, "RX CNT: %lu", rolling_counter_rx);
					ssd1306_SetCursor(2, 24);
					ssd1306_WriteString(lcd_line, Font_6x8, White);
					ssd1306_UpdateScreen();
				}
				else
				{
					PrintTimestamp();
					printf(
							CYAN_TERMINAL"Hash not OK. Let's check within the counter window.\r\n"DEFAULT_TERMINAL);
					rolling_counter_advance = 0;
					is_hash_ok = 0;
					while ((rolling_counter_advance < COUNTER_WINDOW_WIDTH)
							&& (is_hash_ok != 1))
					{
						rolling_counter_advance++;
						dataToBeHashed.Words[1] = rolling_counter_rx
								+ rolling_counter_advance;
						HAL_HASH_Start(&hhash, dataToBeHashed.Bytes, 8,
								SHA256Digest, 100);
						is_hash_ok = 1;
						for (uint8_t i = 0; i < 11; i++)
						{
							is_hash_ok &= (rxData.Bytes[i + 4]
									== SHA256Digest[i]);
						}
					}

					if (is_hash_ok == 1)
					{
						PrintTimestamp();
						printf(
								GREEN_TERMINAL"Counters can be resynchronized. Action granted.\r\n"DEFAULT_TERMINAL);
						HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin,
								GPIO_PIN_SET);
						rolling_counter_rx += rolling_counter_advance;
						rolling_counter_rx++;
						sprintf(lcd_line, "TX CNT: %lu", rolling_counter_rx);
						ssd1306_SetCursor(2, 24);
						ssd1306_WriteString(lcd_line, Font_6x8, White);
						ssd1306_UpdateScreen();
					}
					else
					{
						PrintTimestamp();
						printf(
								RED_TERMINAL"Counters cannot be synchronized! Action denied! Contact your dealer!\r\n"DEFAULT_TERMINAL);
						HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin,
								GPIO_PIN_RESET);
//						Error_Handler();  // :)
					}

				}

			}
			else
			{
				PrintTimestamp();
				printf(
						DEFAULT_TERMINAL"Wrong serial number. Not our key fob.\r\n"DEFAULT_TERMINAL);
				HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin,
						GPIO_PIN_RESET);
			}

		}
		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */
	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct =
	{ 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct =
	{ 0 };

	/** Configure the main internal regulator output voltage
	 */
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

	while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
	{
	}

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_CSI;
	RCC_OscInitStruct.CSIState = RCC_CSI_ON;
	RCC_OscInitStruct.CSICalibrationValue = RCC_CSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_CSI;
	RCC_OscInitStruct.PLL.PLLM = 1;
	RCC_OscInitStruct.PLL.PLLN = 40;
	RCC_OscInitStruct.PLL.PLLP = 2;
	RCC_OscInitStruct.PLL.PLLQ = 2;
	RCC_OscInitStruct.PLL.PLLR = 2;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
	RCC_OscInitStruct.PLL.PLLFRACN = 0;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
	{
		Error_Handler();
	}

	/** Configure the programming delay
	 */
	__HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_1);
}

/* USER CODE BEGIN 4 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
	if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET)
	{
		/* Retreive Rx messages from RX FIFO0 */
		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rxHeader,
				rxData.Bytes) != HAL_OK)
		{
			Error_Handler();
		}
		else
		{
			fob_message_received_flag = 1;
			HAL_GPIO_TogglePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin);
		}

		if (HAL_FDCAN_ActivateNotification(hfdcan,
		FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK)
		{
			/* Notification Error */
			Error_Handler();
		}

	}
}

void PrintTimestamp()
{
	uint32_t time_stamp;
	time_stamp = HAL_GetTick();
	printf("[%03lu.%-3lu] ", time_stamp / 1000, time_stamp % 1000);
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
	/* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
