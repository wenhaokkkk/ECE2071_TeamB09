/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_rx;

TIM_HandleTypeDef htim16;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM16_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
















































/* -------------------- SPI DMA input buffer -------------------- */

//total DMA buffer size = 512 samples
//therefore each DMA half = 256 samples
#define SPI_DMA_INPUT_BUFFER_SIZE 512
uint16_t spiDmaInputBuffer[SPI_DMA_INPUT_BUFFER_SIZE];


/* -------------------- UART TX double buffer -------------------- */

#define UART_PACKED_BUFFER_BYTES 384
uint8_t uartPackedBuffer[2][UART_PACKED_BUFFER_BYTES];
volatile uint8_t uartBufferIndex = 0; //which buffer is being filled as buffer is 2d (representing two halves)
volatile uint8_t uartBusy = 0; //DMA UART in use

//this stores one sample temporarily while waiting for the second sample
//needed to form a 3-byte packed group
volatile uint16_t pendingSampleForPacking = 0;
volatile uint8_t pendingSampleForPackingValid = 0;








/* -------------------- Moving average variables -------------------- */

//defines constant used for the moving average functions and set up
#define MOVING_AVERAGE_SIZE 2
#define MOVING_AVERAGE_SHIFT 1
#define MOVING_AVERAGE_NOT_READY 0xFFFF

//stores the most recent samples used for the moving average. {0} initialises the array as 0
uint16_t movingAverageBuffer[MOVING_AVERAGE_SIZE] = {0};

//stores the sum of the values currently inside movingAverageBuffer. This avoids needing to re-add all values every time
uint32_t movingAverageSum = 0;

//points to the next position in movingAverageBuffer that should be replaced
uint8_t movingAverageIndex = 0;

//counts how many valid samples are currently in the buffer. on start this grows from 0 to MOVING_AVERAGE_SIZE
uint8_t movingAverageCount = 0;
uint8_t movingAverageReady = 0; //becomes 1 when the moving average has reached its correctly set size
uint16_t lastValidFilteredSample = 0; //stores the last valid sample accepted to be sent to the python (used in the outlier detection)








/* -------------------- Outlier Detection Variables -------------------- */

#define OUTLIER_THRESHOLD 3000 //defines the constant which controls at what point a sample is considered an outlier








/* -------------------- Python + command variables -------------------- */

volatile uint8_t pythonCommandByte; //stores the last byte python send (i.e controls the mode)
volatile uint8_t pythonStartRequested = 0; //becomes 1 when the python has requested to begin receiving samples
volatile int firstHalf = 0; //becomes 1 when the first half of the DMA is ready to be proccessed
volatile int secondHalf = 0; //becomes 1 when the second half of the DMA is ready to be proccessed








/* -------------------- Ultrasonic variables -------------------- */

#define ULTRASONIC_PERIOD_MS 60 //time inbetween ultrasonic measurements
#define SOUND_CONSTANT 58
#define TRIGGER_DELAY 10
#define THRESHOLD_DIST 12
#define ECHO_TIMEOUT 700 //saves time by basically setting a max distance the ultrasonic can read

volatile uint8_t objectWithin10cm = 0;
volatile uint16_t latestDistanceCm = 0;
uint32_t lastUltrasonicProcTime = 0;
uint32_t echoStart;
uint32_t echoTime;
uint32_t distance;








/* -------------------- Function prototypes -------------------- */

uint16_t updateMovingAverage(uint16_t newSample);
uint16_t rejectOutliers(uint16_t newSample, uint16_t referenceSample);
void processSpiDmaHalfSamples(uint16_t *sampleBufferStart, uint16_t sampleCount, int firstOrSecond);

void resetPackedSampleState(void);
void packTwo12BitSamples(uint16_t sample1, uint16_t sample2, uint8_t *out);

uint16_t readUltrasonicCm(void);
















/* -------------------- Moving average function -------------------- */

uint16_t updateMovingAverage(uint16_t newSample) {
    //remove the old value currently sitting at this circular-buffer position (containing oldest value) from the running sum
    movingAverageSum -= movingAverageBuffer[movingAverageIndex];

    //store the new sample into the moving average circular buffer
    movingAverageBuffer[movingAverageIndex] = newSample;

    //add the new sample into the running sum
    movingAverageSum += newSample;


    //advance the circular-buffer index
    movingAverageIndex++;
    if (movingAverageIndex >= MOVING_AVERAGE_SIZE) {
        movingAverageIndex = 0;
    }


    //returns constant of moving average not ready when the moving average isnt full up to size yet
    if (movingAverageCount < MOVING_AVERAGE_SIZE) {
        movingAverageCount++;

        if (movingAverageCount < MOVING_AVERAGE_SIZE) {
            return MOVING_AVERAGE_NOT_READY;
        }
    }

    //once the moving average is fully warm, Right shift by moving average shift => divide by moving average size
    return (uint16_t)(movingAverageSum >> MOVING_AVERAGE_SHIFT);
}








/* -------------------- Outlier Rejection function -------------------- */

uint16_t rejectOutliers(uint16_t newSample, uint16_t referenceSample) {
    int16_t difference;

    difference = newSample - referenceSample;

    if (difference < 0) {
        difference = -difference;
    }

    if (difference > OUTLIER_THRESHOLD) {
        return referenceSample;
    }

    return newSample;
}








/* -------------------- Python Send functions -------------------- */

void resetPackedSampleState(void) {
    pendingSampleForPackingValid = 0;
}


void packTwo12BitSamples(uint16_t sample1, uint16_t sample2, uint8_t *packedBuffer) {
	//ensures each sample is only the bottom 12 bits
    sample1 &= 0x0FFF;
    sample2 &= 0x0FFF;


    // Pack two 12-bit samples into three bytes
    // 	sample1:
    // 		bits [11:4] (the upper 8 bits of sample 1) go into out[0]
    // 		bits [3:0]  (the lower 4 bits of sample 1) go into the upper half of out[1]
    //
    // 	sample2:
    //   	bits [11:8] (the upper 4 bits of sample 2) go into the lower half of out[1]
    //   	bits [7:0]  (the lower 8 bits of sample 2) go into out[2]
    //
    //  Layout:
    //   	out[0] = sample1[11:4]
    //   	out[1] = sample1[3:0] sample2[11:8]
    //  	out[2] = sample2[7:0]
    //
    packedBuffer[0] = (sample1 >> 4) & 0xFF;
    packedBuffer[1] = ((sample1 << 4) & 0x0F0) | ((sample2 >> 8) & 0x0F);
    packedBuffer[2] = sample2 & 0xFF;
}








/* -------------------- SPI DMA processing -------------------- */

void processSpiDmaHalfSamples(uint16_t *sampleBufferStart, uint16_t sampleCount, int firstOrSecond) {
    uint16_t rawSample;
    uint16_t filteredSample;
    uint16_t averagedSample;
    uint16_t packedByteIndex = 0;
    uint8_t *uartSendingBuffer = uartPackedBuffer[uartBufferIndex];

    for (uint16_t i = 0; i < sampleCount; i++) {
        if (!pythonStartRequested) {
        	return;
        }

        if (pythonCommandByte == 'D' && !objectWithin10cm) {
        	return;
        }

        //ensures the samples contains only the bottom 12 bits
        rawSample = sampleBufferStart[i] & 0x0FFF;

        //does outlier rejection, compares against last known good averaged sample
        if (movingAverageReady) {
            filteredSample = rejectOutliers(rawSample, lastValidFilteredSample);
        } else {
            filteredSample = rawSample;
        }

        //does moving average filter
        averagedSample = updateMovingAverage(filteredSample);

        //skip samples during moving average warm-up
        if (averagedSample == MOVING_AVERAGE_NOT_READY) {
            continue;
        }

        movingAverageReady = 1;
        lastValidFilteredSample = averagedSample;

        //packs each sample with a previously stored sample (if doesnt exist, saves current sample as 'previously stored sample'
        if (!pendingSampleForPackingValid) {
            pendingSampleForPacking = averagedSample;
            pendingSampleForPackingValid = 1;
        } else {
            packTwo12BitSamples(pendingSampleForPacking, averagedSample, &uartSendingBuffer[packedByteIndex]);
            packedByteIndex += 3;
            pendingSampleForPackingValid = 0;
        }
    }


    //sends the packed bytes if the buffer has any packed bytes within it
    if (packedByteIndex > 0) {
        //wait for previous DMA send to finish before starting next one
        //at 921600 baud, 384 bytes takes ~3.3ms. Half-buffer arrives every ~5.8ms
        //so this wait should be very short or zero in normal operation
        while (uartBusy);

        uartBusy = 1;
        uartBufferIndex ^= 1; //flips 2d array index by using  the xor operator, i.e if was 0, goes 1 and vice versa
        HAL_UART_Transmit_DMA(&huart2, uartSendingBuffer, packedByteIndex); //sends to the python using DMA
    }

    //resets the flag of whichever called the function
    if (firstOrSecond) {
    	firstHalf = 0;
    } else {
    	secondHalf = 0;
    }
}








/* -------------------- Ultrasonic Functions -------------------- */

uint16_t readUltrasonicCm(void) {
    //send 10us trigger pulse
    HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, 1);
    __HAL_TIM_SET_COUNTER(&htim16, 0);
    while(__HAL_TIM_GET_COUNTER(&htim16) < TRIGGER_DELAY);
    HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, 0);

    //wait for echo to go high
    while(HAL_GPIO_ReadPin(Echo_GPIO_Port, Echo_Pin) == GPIO_PIN_RESET);

    //reset counter and wait for echo to go low
    __HAL_TIM_SET_COUNTER(&htim16, 0);
    while(HAL_GPIO_ReadPin(Echo_GPIO_Port, Echo_Pin) == GPIO_PIN_SET) {
    	//implements a type of timeout to only measure for as long as needed to detect the correct distance
    	if(__HAL_TIM_GET_COUNTER(&htim16) > ECHO_TIMEOUT) {

			distance = 999;
			if(distance < THRESHOLD_DIST) {
				HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 1);
			} else {
				HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 0);
			}
			return distance;

    	}
    }

    echoTime = __HAL_TIM_GET_COUNTER(&htim16);
    //calculate distance
    distance = echoTime / SOUND_CONSTANT;

    if(distance < THRESHOLD_DIST) {
        HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 1);
    } else {
        HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 0);
    }
    return distance;
}















































































































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
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  MX_TIM16_Init();
  /* USER CODE BEGIN 2 */











  //ensures the receive DMA is set up correctly
  if (HAL_SPI_Receive_DMA(&hspi1, spiDmaInputBuffer, SPI_DMA_INPUT_BUFFER_SIZE) != HAL_OK) {
      Error_Handler();
  }

  //ensures the receive command byte from python is set up correctly
  if (HAL_UART_Receive_IT(&huart2, &pythonCommandByte, 1) != HAL_OK) {
      Error_Handler();
  }

  HAL_TIM_Base_Start(&htim16);



  /* USER CODE END 2 */







  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {

      if (pythonCommandByte == 'D') {
          if (HAL_GetTick() - lastUltrasonicProcTime >= ULTRASONIC_PERIOD_MS) {
              lastUltrasonicProcTime = HAL_GetTick();
              latestDistanceCm = readUltrasonicCm();
              objectWithin10cm = latestDistanceCm < 12;
          }
      }

      if (firstHalf) {
          processSpiDmaHalfSamples(&spiDmaInputBuffer[0], SPI_DMA_INPUT_BUFFER_SIZE / 2, 1);
      }
      if (secondHalf) {
          processSpiDmaHalfSamples(&spiDmaInputBuffer[SPI_DMA_INPUT_BUFFER_SIZE / 2], SPI_DMA_INPUT_BUFFER_SIZE / 2, 0);
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
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES_RXONLY;
  hspi1.Init.DataSize = SPI_DATASIZE_16BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM16 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM16_Init(void)
{

  /* USER CODE BEGIN TIM16_Init 0 */

  /* USER CODE END TIM16_Init 0 */

  /* USER CODE BEGIN TIM16_Init 1 */

  /* USER CODE END TIM16_Init 1 */
  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 31;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 65535;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM16_Init 2 */

  /* USER CODE END TIM16_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 921600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
  /* DMA1_Channel7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel7_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD3_Pin|Trigger_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LD3_Pin Trigger_Pin */
  GPIO_InitStruct.Pin = LD3_Pin|Trigger_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : Echo_Pin */
  GPIO_InitStruct.Pin = Echo_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Echo_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

























/* USER CODE BEGIN 4 */
/* -------------------- UART DMA callback -------------------- */

//is called when the DMA successfully finishes sending the buffer passed to it over the UART
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        uartBusy = 0;
    }
}








/* -------------------- SPI DMA callbacks -------------------- */

void HAL_SPI_RxHalfCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance == SPI1) {
        //DMA has filled the FIRST HALF of spiDmaInputBuffer
    	firstHalf = 1;
    }
}


void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance == SPI1) {
        //DMA has filled the SECOND HALF of spiDmaInputBuffer
    	secondHalf = 1;
    }
}








/* -------------------- UART command callback from Python -------------------- */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {

        if (pythonCommandByte == 'S') {
            resetPackedSampleState();
            pythonStartRequested = 0;
            HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, 0);

        } else if (pythonCommandByte == 'M') {
            resetPackedSampleState();
            pythonStartRequested = 1;

        } else if (pythonCommandByte == 'D') {
            resetPackedSampleState();
            pythonStartRequested = 1;

            lastUltrasonicProcTime = HAL_GetTick();
            latestDistanceCm = readUltrasonicCm();
            objectWithin10cm = latestDistanceCm < 12;
        }

        //re-arm UART receive interrupt so the next Python command can be received
        HAL_UART_Receive_IT(&huart2, &pythonCommandByte, 1);
    }
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

#ifdef  USE_FULL_ASSERT
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
