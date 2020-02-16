/*
 * usbimager/disks_win.c
 *
 * Copyright (C) 2020 bzt (bztsrc@gitlab)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * @brief Disk iteration and umount for Windows
 *
 */

#include <windows.h>
#include <winioctl.h>
#include <commctrl.h>
#include <ddk/ntdddisk.h>
#include "resource.h"
#include "disks.h"

extern int verbose;

int disks_targets[DISKS_MAX];

HANDLE hTargetVolume = NULL;

/**
 * Refresh target device list in the combobox
 */
void disks_refreshlist(void *data) {
    HWND hwndDlg = (HWND)data;
    int i = 0, j;
    TCHAR szLbText[1024];
    HANDLE hTargetDevice;
    DISK_GEOMETRY diskGeometry;
    DWORD bytesReturned;
    long long int totalNumberOfBytes = 0;
    STORAGE_PROPERTY_QUERY Query;
    char Buf[1024] = {0}, letter, *c, siz[64], fn[64] = "\\\\.\\X:";
    PSTORAGE_DEVICE_DESCRIPTOR pDevDesc = (PSTORAGE_DEVICE_DESCRIPTOR)Buf;
    pDevDesc->Size = sizeof(Buf);
    Query.PropertyId = StorageDeviceProperty;
    Query.QueryType = PropertyStandardQuery;

    memset(disks_targets, 0xff, sizeof(disks_targets));
#if DISKS_TEST
    disks_targets[i++] = 'T';
    sprintf(siz, "T: Testfile .\\test.bin");
    for(c = siz; *c; c++, j++) szLbText[j] = (TCHAR)*c;
    SendDlgItemMessage(hwndDlg, IDC_MAINDLG_TARGET_LIST, CB_ADDSTRING, 0, (LPARAM) szLbText);
#endif
    for(letter = 'A'; letter <= 'Z'; letter++) {
        fn[4] = letter;
        /* fn[6] = '\\'; if(GetDriveType(fn) != DRIVE_REMOVABLE) continue; else fn[6] = 0; */
        if(letter == 'C') continue;
        hTargetDevice = CreateFileA(fn, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hTargetDevice != INVALID_HANDLE_VALUE) {
            totalNumberOfBytes = 0;
            if (DeviceIoControl(hTargetDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &diskGeometry, sizeof diskGeometry, &bytesReturned, NULL)) {
                totalNumberOfBytes = (long long int)diskGeometry.Cylinders.QuadPart * (long long int)diskGeometry.TracksPerCylinder * (long long int)diskGeometry.SectorsPerTrack * (long long int)diskGeometry.BytesPerSector;
            }
            szLbText[0] = (TCHAR)letter; szLbText[1] = (TCHAR)':';
            j = 2;
            if (totalNumberOfBytes > 0) {
                ULONG sizeInGbTimes10 = (ULONG)(10 * (totalNumberOfBytes + 1024L*1024L*1024L-1L) / 1024L / 1024L / 1024L);
                sprintf(siz, " [%lu.%lu GiB]", (ULONG)(sizeInGbTimes10 / 10), (ULONG)(sizeInGbTimes10 % 10));
                for(c = siz; *c; c++, j++) szLbText[j] = (TCHAR)*c;
            }
            if (DeviceIoControl(hTargetDevice, IOCTL_STORAGE_QUERY_PROPERTY, &Query, 
                sizeof(STORAGE_PROPERTY_QUERY), pDevDesc, pDevDesc->Size, &bytesReturned,  (LPOVERLAPPED)NULL)) {
                if (pDevDesc->VendorIdOffset != 0) {
                    szLbText[j++] = (TCHAR)' ';
                    for(c = (PCHAR)((PBYTE)pDevDesc + pDevDesc->VendorIdOffset); *c; c++, j++) szLbText[j] = (TCHAR)*c;
                }
                if (pDevDesc->ProductIdOffset != 0) {
                    szLbText[j++] = (TCHAR)' ';
                    for(c = (PCHAR)((PBYTE)pDevDesc + pDevDesc->ProductIdOffset); *c; c++, j++) szLbText[j] = (TCHAR)*c;
                }
            }
            CloseHandle(hTargetDevice);
            disks_targets[i++] = letter;
            SendDlgItemMessage(hwndDlg, IDC_MAINDLG_TARGET_LIST, CB_ADDSTRING, 0, (LPARAM) szLbText);
            if(i >= DISKS_MAX) break;
        }
    }
}

/**
 * Lock, umount and open the target disk for writing
 */
void *disks_open(int targetId)
{
    HANDLE ret;
    char szVolumePathName[8] = "\\\\.\\X:", szDevicePathName[32];
    VOLUME_DISK_EXTENTS volumeDiskExtents;
    DWORD bytesReturned;

    if(targetId < 0 || targetId >= DISKS_MAX || disks_targets[targetId] == -1 || disks_targets[targetId] == 'C') return (HANDLE)-1;

#if DISKS_TEST
    if((char)disks_targets[targetId] == 'T') {
        hTargetVolume = NULL;
        ret = CreateFileA(".\\test.bin", FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 0, 0, NULL);
        return (void*)ret;
    }
#endif
    szVolumePathName[4] = (char)disks_targets[targetId];
    hTargetVolume = CreateFileA(szVolumePathName, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hTargetVolume == INVALID_HANDLE_VALUE) return NULL;

    if (DeviceIoControl(hTargetVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        DeviceIoControl(hTargetVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &volumeDiskExtents, sizeof volumeDiskExtents, &bytesReturned, NULL);
        DeviceIoControl(hTargetVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        sprintf(szDevicePathName, "\\\\.\\PhysicalDrive%u", (unsigned int)volumeDiskExtents.Extents[0].DiskNumber);
        ret = CreateFileA(szDevicePathName, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (ret == INVALID_HANDLE_VALUE) {
            DeviceIoControl(hTargetVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
            CloseHandle(hTargetVolume);
            hTargetVolume = NULL;
            return NULL;
        }
    } else {
        CloseHandle(hTargetVolume);
        hTargetVolume = NULL;
        return NULL;
    }
    return (void*)ret;
}

/**
 * Close the target disk
 */
void disks_close(void *data)
{
    DWORD bytesReturned;

    CloseHandle((HANDLE)data);

    if(hTargetVolume) {
        DeviceIoControl(hTargetVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        CloseHandle(hTargetVolume);
        hTargetVolume = NULL;
    }
}
