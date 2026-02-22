/****************************************************************************
 * Adapted from the LUFA Library:
 *
 *   Copyright 2011  Dean Camera (dean [at] fourwalledcubicle [dot] com)
 *   dean [at] fourwalledcubicle [dot] com, www.lufa-lib.org
 *
 * Permission to use, copy, modify, distribute, and sell this
 * software and its documentation for any purpose is hereby granted
 * without fee, provided that the above copyright notice appear in
 * all copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 *
 ****************************************************************************/

#include "hid_parser.h"
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#ifdef DYNAMIC
#include <malloc.h>
#define ACQUIRE_AND_RELEASE(structure, size) \
__attribute__ ((always_inline)) static inline structure##_t* acquire_##structure() { return malloc(sizeof(structure##_t)); } \
__attribute__ ((always_inline)) static inline void release_##structure(structure##_t* pointer) { free(pointer); }
#else
#define ACQUIRE_AND_RELEASE(structure, size) \
enum { MAX_##structure =  size }; \
static structure##_t structure##s[MAX_##structure]; \
static bool structure##sAcquired[MAX_##structure]; \
static structure##_t* acquire_##structure() \
{\
	for(uint8_t i=0; i < MAX_##structure; i++) \
	{\
	if(!structure##sAcquired[i])\
		{\
			structure##sAcquired[i] = true;\
			return &structure##s[i];\
		}\
	}\
	assert(false);\
	return NULL;\
}\
static void release_##structure(structure##_t* pointer)\
{\
	structure##sAcquired[pointer - structure##s] = false;\
}
#endif

ACQUIRE_AND_RELEASE(HID_ReportSizeInfo, 100);
ACQUIRE_AND_RELEASE(HID_CollectionPath, 25);
ACQUIRE_AND_RELEASE(HID_ReportInfo, 1);
ACQUIRE_AND_RELEASE(HID_ReportItem, 50);

void USB_FreeReportInfo(HID_ReportInfo_t *ReportInfo)
{
	if (ReportInfo)
	{
		HID_ReportItem_t *item = ReportInfo->FirstReportItem;
		while (item)
		{
			HID_ReportItem_t *current = item;
			item = item->Next;
			release_HID_ReportItem(current);
		}
		release_HID_ReportInfo(ReportInfo);
	}
}

uint8_t USB_ProcessHIDReport(uint8_t dev_addr,
							 uint8_t instance,
							 const uint8_t *ReportData,
							 uint16_t ReportSize,
							 HID_ReportInfo_t **ParserDataOut)
{
	HID_ReportSizeInfo_t *FirstReportIDSize = acquire_HID_ReportSizeInfo();
	HID_CollectionPath_t *FirstCollectionPath = acquire_HID_CollectionPath();
	memset(FirstCollectionPath, 0, sizeof(HID_CollectionPath_t));
	HID_ReportInfo_t *ParserData = acquire_HID_ReportInfo();
	HID_StateTable_t StateTable[HID_STATETABLE_STACK_DEPTH];
	HID_StateTable_t *CurrStateTable = &StateTable[0];
	HID_CollectionPath_t *CurrCollectionPath = NULL;
	HID_ReportSizeInfo_t *CurrReportIDInfo = FirstReportIDSize;
	uint16_t UsageList[HID_USAGE_STACK_DEPTH];
	uint8_t UsageListSize = 0;
	HID_MinMax_t UsageMinMax = {0, 0};

	memset(ParserData, 0x00, sizeof(HID_ReportInfo_t));
	memset(CurrStateTable, 0x00, sizeof(HID_StateTable_t));
	memset(CurrReportIDInfo, 0x00, sizeof(HID_ReportSizeInfo_t));

	ParserData->TotalDeviceReports = 1;
	uint8_t Result = HID_PARSE_Successful;

	while (ReportSize)
	{
		uint8_t HIDReportItem = *ReportData;
		uint32_t ReportItemData;

		ReportData++;
		ReportSize--;

		switch (HIDReportItem & HID_RI_DATA_SIZE_MASK)
		{
			case HID_RI_DATA_BITS_32:
				ReportItemData = (((uint32_t)ReportData[3] << 24) | ((uint32_t)ReportData[2] << 16) |
								((uint16_t)ReportData[1] << 8) | ReportData[0]);
				ReportSize -= 4;
				ReportData += 4;
				break;

			case HID_RI_DATA_BITS_16:
				ReportItemData = (((uint16_t)ReportData[1] << 8) | (ReportData[0]));
				ReportSize -= 2;
				ReportData += 2;
				break;

			case HID_RI_DATA_BITS_8:
				ReportItemData = ReportData[0];
				ReportSize -= 1;
				ReportData += 1;
				break;

			default:
				ReportItemData = 0;
				break;
		}

		switch (HIDReportItem & (HID_RI_TYPE_MASK | HID_RI_TAG_MASK))
		{
			case HID_RI_PUSH(0):
				if (CurrStateTable == &StateTable[HID_STATETABLE_STACK_DEPTH - 1])
				{
					Result = HID_PARSE_HIDStackOverflow;
					break;
				}

				memcpy((CurrStateTable + 1),
					CurrStateTable,
					sizeof(HID_StateTable_t));

				CurrStateTable++;
				break;

			case HID_RI_POP(0):
				if (CurrStateTable == &StateTable[0])
				{
					Result = HID_PARSE_HIDStackUnderflow;
					break;
				}

				CurrStateTable--;
				break;

			case HID_RI_USAGE_PAGE(0):
				CurrStateTable->Attributes.Usage.Page = ReportItemData;
				break;

			case HID_RI_LOGICAL_MINIMUM(0):
				CurrStateTable->Attributes.Logical.Minimum = ReportItemData;
				break;

			case HID_RI_LOGICAL_MAXIMUM(0):
				CurrStateTable->Attributes.Logical.Maximum = ReportItemData;
				break;

			case HID_RI_PHYSICAL_MINIMUM(0):
				CurrStateTable->Attributes.Physical.Minimum = ReportItemData;
				break;

			case HID_RI_PHYSICAL_MAXIMUM(0):
				CurrStateTable->Attributes.Physical.Maximum = ReportItemData;
				break;

			case HID_RI_UNIT_EXPONENT(0):
				CurrStateTable->Attributes.Unit.Exponent = ReportItemData;
				break;

			case HID_RI_UNIT(0):
				CurrStateTable->Attributes.Unit.Type = ReportItemData;
				break;

			case HID_RI_REPORT_SIZE(0):
				CurrStateTable->Attributes.BitSize = ReportItemData;
				break;

			case HID_RI_REPORT_COUNT(0):
				CurrStateTable->ReportCount = ReportItemData;
				break;

			case HID_RI_REPORT_ID(0):
				CurrStateTable->ReportID = ReportItemData;

				if (ParserData->UsingReportIDs)
				{
					CurrReportIDInfo = NULL;
					HID_ReportSizeInfo_t *iterator = FirstReportIDSize;
					while (true)
					{
						if (iterator->ReportID == CurrStateTable->ReportID)
						{
							CurrReportIDInfo = iterator;
							break;
						}
						if (!iterator->Next)
							break;
						iterator = iterator->Next;
					}

					if (CurrReportIDInfo == NULL)
					{
						ParserData->TotalDeviceReports++;
						iterator->Next = CurrReportIDInfo = acquire_HID_ReportSizeInfo();
						memset(CurrReportIDInfo, 0x00, sizeof(HID_ReportSizeInfo_t));
					}
				}

				ParserData->UsingReportIDs = true;

				CurrReportIDInfo->ReportID = CurrStateTable->ReportID;
				break;

			case HID_RI_USAGE(0):
				if (UsageListSize == HID_USAGE_STACK_DEPTH)
				{
					Result = HID_PARSE_UsageListOverflow;
					break;
				}

				if ((HIDReportItem & HID_RI_DATA_SIZE_MASK) == HID_RI_DATA_BITS_32)
					CurrStateTable->Attributes.Usage.Page = (ReportItemData >> 16);

				UsageList[UsageListSize++] = ReportItemData;
				break;

			case HID_RI_USAGE_MINIMUM(0):
				UsageMinMax.Minimum = ReportItemData;
				break;

			case HID_RI_USAGE_MAXIMUM(0):
				UsageMinMax.Maximum = ReportItemData;
				break;

			case HID_RI_COLLECTION(0):
				if (CurrCollectionPath == NULL)
				{
					CurrCollectionPath = FirstCollectionPath;
				}
				else
				{
					HID_CollectionPath_t *ParentCollectionPath = CurrCollectionPath;

					CurrCollectionPath = FirstCollectionPath;
					while (CurrCollectionPath->Next)
					{
						CurrCollectionPath = CurrCollectionPath->Next;
					}
					HID_CollectionPath_t *NewCollectionPath = acquire_HID_CollectionPath();
					CurrCollectionPath->Next = NewCollectionPath;
					CurrCollectionPath = NewCollectionPath;
					memset(CurrCollectionPath, 0, sizeof(HID_CollectionPath_t));
					CurrCollectionPath->Parent = ParentCollectionPath;
				}

				CurrCollectionPath->Type = ReportItemData;
				CurrCollectionPath->Usage.Page = CurrStateTable->Attributes.Usage.Page;

				if (UsageListSize)
				{
					CurrCollectionPath->Usage.Usage = UsageList[0];

					for (uint8_t i = 1; i < UsageListSize; i++)
						UsageList[i - 1] = UsageList[i];

					UsageListSize--;
				}
				else if (UsageMinMax.Minimum <= UsageMinMax.Maximum)
				{
					CurrCollectionPath->Usage.Usage = UsageMinMax.Minimum++;
				}

				break;

			case HID_RI_END_COLLECTION(0):
				if (CurrCollectionPath == NULL)
				{
					Result = HID_PARSE_UnexpectedEndCollection;
					break;
				}
				CurrCollectionPath = CurrCollectionPath->Parent;
				if (CurrCollectionPath)
				{
					release_HID_CollectionPath(CurrCollectionPath->Next);
					CurrCollectionPath->Next = NULL;
				}
				break;

			case HID_RI_INPUT(0):
			case HID_RI_OUTPUT(0):
			case HID_RI_FEATURE(0):
				for (uint8_t ReportItemNum = 0; ReportItemNum < CurrStateTable->ReportCount; ReportItemNum++)
				{
					HID_ReportItem_t NewReportItem;

					memcpy(&NewReportItem.Attributes,
						&CurrStateTable->Attributes,
						sizeof(HID_ReportItem_Attributes_t));

					NewReportItem.ItemFlags = ReportItemData;
					NewReportItem.ReportID = CurrStateTable->ReportID;
					NewReportItem.ReportCount = CurrStateTable->ReportCount;
					if (UsageListSize)
					{
						NewReportItem.Attributes.Usage.Usage = UsageList[0];

						for (uint8_t i = 1; i < UsageListSize; i++)
							UsageList[i - 1] = UsageList[i];

						UsageListSize--;
					}
					else if (UsageMinMax.Minimum <= UsageMinMax.Maximum)
					{
						NewReportItem.Attributes.Usage.Usage = UsageMinMax.Minimum++;
					}

					uint8_t ItemTypeTag = (HIDReportItem & (HID_RI_TYPE_MASK | HID_RI_TAG_MASK));

					if (ItemTypeTag == HID_RI_INPUT(0))
						NewReportItem.ItemType = HID_REPORT_ITEM_In;
					else if (ItemTypeTag == HID_RI_OUTPUT(0))
						NewReportItem.ItemType = HID_REPORT_ITEM_Out;
					else
						NewReportItem.ItemType = HID_REPORT_ITEM_Feature;

					NewReportItem.BitOffset = CurrReportIDInfo->ReportSizeBits[NewReportItem.ItemType];

					CurrReportIDInfo->ReportSizeBits[NewReportItem.ItemType] += CurrStateTable->Attributes.BitSize;

					ParserData->LargestReportSizeBits = MAX(ParserData->LargestReportSizeBits, CurrReportIDInfo->ReportSizeBits[NewReportItem.ItemType]);

					if (!(ReportItemData & HID_IOF_CONSTANT) && CALLBACK_HIDParser_FilterHIDReportItem(dev_addr, instance, &NewReportItem))
					{
						if (!ParserData->FirstReportItem)
						{
							ParserData->FirstReportItem = acquire_HID_ReportItem();
							ParserData->LastReportItem = ParserData->FirstReportItem;
						}
						else
						{
							ParserData->LastReportItem->Next = acquire_HID_ReportItem();
							ParserData->LastReportItem = ParserData->LastReportItem->Next;
						}
						memcpy(ParserData->LastReportItem, &NewReportItem, sizeof(HID_ReportItem_t));
						ParserData->LastReportItem->Next = NULL;
						ParserData->TotalReportItems++;
					}
				}
				break;

			default:
				break;
		}
		if (Result != HID_PARSE_Successful)
		{
			break;
		}

		if ((HIDReportItem & HID_RI_TYPE_MASK) == HID_RI_TYPE_MAIN)
		{
			UsageMinMax.Minimum = 0;
			UsageMinMax.Maximum = 0;
			UsageListSize = 0;
		}
	}

	if (!(ParserData->TotalReportItems))
		Result = HID_PARSE_NoUnfilteredReportItems;
	if (Result != HID_PARSE_Successful)
	{
		USB_FreeReportInfo(ParserData);
	}
	else
	{
		*ParserDataOut = ParserData;
	}

	HID_ReportSizeInfo_t *iteratorReportIDSizes = FirstReportIDSize;
	while (iteratorReportIDSizes)
	{
		HID_ReportSizeInfo_t *temp = iteratorReportIDSizes->Next;
		release_HID_ReportSizeInfo(iteratorReportIDSizes);
		iteratorReportIDSizes = temp;
	}

	HID_CollectionPath_t *iteratorCollectionPaths = FirstCollectionPath;
	while (iteratorCollectionPaths)
	{
		HID_CollectionPath_t *temp = iteratorCollectionPaths->Next;
		release_HID_CollectionPath(iteratorCollectionPaths);
		iteratorCollectionPaths = temp;
	}
	return Result;
}

// Default filter: accept gamepad axes, hat, dpad, buttons, mouse, keyboard
bool CALLBACK_HIDParser_FilterHIDReportItem(uint8_t dev_addr, uint8_t instance,
                                             HID_ReportItem_t *const CurrentItem)
{
	(void)dev_addr;
	(void)instance;

	if (CurrentItem->ItemType != HID_REPORT_ITEM_In)
		return false;

	switch (CurrentItem->Attributes.Usage.Page)
	{
		case 0x01:  // Generic Desktop
			switch (CurrentItem->Attributes.Usage.Usage)
			{
				case 0x30: case 0x31: case 0x32:  // X, Y, Z
				case 0x33: case 0x34: case 0x35:  // RX, RY, RZ
				case 0x39:  // Hat switch
				case 0x90: case 0x91: case 0x92: case 0x93:  // DPad U/D/R/L
				case 0x38:  // Wheel
				case 0x02:  // Mouse
				case 0x06:  // Keyboard
					return true;
			}
			return false;
		case 0x02:  // Simulation Controls (Xbox triggers: Brake/Accelerator)
			switch (CurrentItem->Attributes.Usage.Usage)
			{
				case 0xC4:  // Accelerator
				case 0xC5:  // Brake
					return true;
			}
			return false;
		case 0x09:  // Button
			return true;
	}
	return false;
}

bool USB_GetHIDReportItemInfo(uint16_t report_id, const uint8_t *ReportData,
							  HID_ReportItem_t *const ReportItem)
{
	if (ReportItem == NULL)
	{
		return false;
	}

	if (ReportItem->ReportID != report_id)
	{
		return false;
	}

	uint16_t DataBitsRem = ReportItem->Attributes.BitSize;
	uint16_t CurrentBit = ReportItem->BitOffset;
	uint32_t BitMask = (1 << 0);

	ReportItem->PreviousValue = ReportItem->Value;
	ReportItem->Value = 0;

	while (DataBitsRem--)
	{
		if (ReportData[CurrentBit / 8] & (1 << (CurrentBit % 8)))
			ReportItem->Value |= BitMask;

		CurrentBit++;
		BitMask <<= 1;
	}

	return true;
}