#include "r0SerialWrite.h"

HANDLE hThread = NULL;
HANDLE hSerial = NULL;

BOOLEAN CompareDescription(_In_ PDEVICE_OBJECT dev, _In_ LPCWSTR desc)
{
	BOOLEAN isFound = FALSE;
	DEVICE_REGISTRY_PROPERTY devProp = DevicePropertyDeviceDescription;
	ULONG resultLength = 0;

	NTSTATUS status = IoGetDeviceProperty(dev, devProp, 0, NULL, &resultLength);
	if (status == STATUS_BUFFER_TOO_SMALL && resultLength)
	{
		LPWSTR valueInfo = ExAllocatePool(NonPagedPool, resultLength);
		if (valueInfo)
		{
			status = IoGetDeviceProperty(dev, devProp, resultLength, valueInfo, &resultLength);
			if (NT_SUCCESS(status))
			{
				if (_wcsicmp(valueInfo, desc) == 0)
				{
					isFound = TRUE;
					DbgPrintEx(0, 0, "[dbg] Found device %ws\n", valueInfo);
				}
			}
			ExFreePool(valueInfo);
		}
	}

	return isFound;
}
PDEVICE_OBJECT GetDevicePhysObj(_In_ LPCWSTR desc)
{
	PDEVICE_OBJECT pRet = NULL;
	PDRIVER_OBJECT pDriverObj = NULL;
	PDEVICE_OBJECT pDeviceObj = NULL;
	UNICODE_STRING usDrv = RTL_CONSTANT_STRING(L"\\Driver\\usbccgp"); //usbccgp //USBHUB3

	NTSTATUS status = ObReferenceObjectByName(&usDrv, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, &pDriverObj);
	if (NT_SUCCESS(status))
	{
		for (pDeviceObj = pDriverObj->DeviceObject; pDeviceObj; pDeviceObj = pDeviceObj->NextDevice)
		{
			if (CompareDescription(pDeviceObj, desc) && pDeviceObj->AttachedDevice)
			{
				pRet = pDeviceObj->AttachedDevice;
				break;
			}
		}
		ObDereferenceObject(pDriverObj);
	}

	return pRet;
}
BOOLEAN OpenDeviceHandle(_In_ PDEVICE_OBJECT pdo, _In_ PHANDLE handle)
{
	BOOLEAN isOpened = FALSE;
	IO_STATUS_BLOCK iosb;
	OBJECT_ATTRIBUTES attr;
	ULONG returnLength = 0;

	NTSTATUS status = ObQueryNameString(pdo, NULL, 0, &returnLength);
	if (status == STATUS_INFO_LENGTH_MISMATCH)
	{
		PUNICODE_STRING pUsDev = ExAllocatePool(NonPagedPool, returnLength);
		if (pUsDev)
		{
			status = ObQueryNameString(pdo, (POBJECT_NAME_INFORMATION)pUsDev, returnLength, &returnLength);
			if (NT_SUCCESS(status) && pUsDev->Length && pUsDev->MaximumLength && pUsDev)
			{
				InitializeObjectAttributes(&attr, pUsDev, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
				status = ZwCreateFile(handle, GENERIC_WRITE, &attr, &iosb, NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
				if (NT_SUCCESS(status))
				{
					isOpened = TRUE;
					DbgPrintEx(0, 0, "[dbg] Opened handle to %wZ\n", pUsDev);
				}
			}
			ExFreePool(pUsDev);
		}
	}

	return isOpened;
}

VOID TestThread()
{
	UCHAR buffer[4] = "kek";
	IO_STATUS_BLOCK iosb;
	NTSTATUS status = ZwWriteFile(hSerial, NULL, NULL, NULL, &iosb, buffer, sizeof(buffer), NULL, NULL);
	DbgPrintEx(0, 0, "[dbg] Write status: 0x%08x\n", status);
}
VOID DriverUnload(_In_ PDRIVER_OBJECT pDrvObj)
{
	UNREFERENCED_PARAMETER(pDrvObj);
	if (hThread)
	{
		ZwWaitForSingleObject(hThread, FALSE, NULL);
		ZwClose(hThread);
	}
	if (hSerial) { ZwClose(hSerial); }
	DbgPrintEx(0, 0, "[dbg] Driver unloaded\n");
}
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT pDrvObj, _In_ PUNICODE_STRING pRegPath)
{
	UNREFERENCED_PARAMETER(pRegPath);
	pDrvObj->DriverUnload = DriverUnload;
	NTSTATUS status = STATUS_UNSUCCESSFUL;

	PDEVICE_OBJECT pSerial = GetDevicePhysObj(L"Arduino Leonardo");
	if (!pSerial) { return status; }
	DbgPrintEx(0, 0, "[dbg] pSerial: 0x%p\n", pSerial);

	if (!OpenDeviceHandle(pSerial, &hSerial)) { return status; }
	DbgPrintEx(0, 0, "[dbg] hSerial: 0x%p\n", hSerial);

	status = PsCreateSystemThread(&hThread, STANDARD_RIGHTS_ALL, NULL, NULL, NULL, (PKSTART_ROUTINE)TestThread, NULL);
	DbgPrintEx(0, 0, "[dbg] Thread status: 0x%08x\n", status);

	return status;
}