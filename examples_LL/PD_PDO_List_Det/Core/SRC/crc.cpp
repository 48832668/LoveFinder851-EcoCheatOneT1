/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    crc.cpp
  * @brief   This file provides code for the configuration
  *          of all used CRC.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 Puya Semiconductor Co.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by Puya under BSD 3-Clause license,
  * the License ; You may not use this file except in compliance with the
  * License.You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "crc.hpp"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Public variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Private */

/* USER CODE END Private */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/***************************************
 * @brief CRC Init
 *        HAL_CRC_Init() 本身只做两件事：
 *          1) 通过 HAL_CRC_MspInit() 使能 CRC 时钟
 *          2) 把句柄状态置为 READY
 *        因此 LL 等价实现就是使能时钟（下面第 2 行是等价的确定性复位，
 *        HAL 版没有做，如不需要可以删掉）。
 *
 *        数据写入 / 读取：
 *          LL_CRC_FeedData32(CRC, data)
 *          LL_CRC_ReadData32(CRC)
 **************************************/
void Studio_CRC_Init(void)
{
  /* USER CODE BEGIN Studio_CRC_Init 0 */

  /* USER CODE END Studio_CRC_Init 0 */

  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_CRC);
  LL_CRC_ResetCRCCalculationUnit(CRC);

  /* USER CODE BEGIN Studio_CRC_Init 1 */

  /* USER CODE END Studio_CRC_Init 1 */
}

/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
