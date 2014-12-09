' Visual Studio QEMU debugging script.
'
' I like invoking vbs as much as anyone else, but we need to download and unzip our
' EFI BIOS file, as well as launch QEMU, and neither Powershell or a standard batch
' can do that without having an extra console appearing.
'
' Note: You may get a prompt from the firewall when trying to download the BIOS file

' Modify these variables if needed
OVMF_ZIP   = "OVMF-X64-r15214.zip"
OVMF_BIOS  = "OVMF.fd"
FTP_SERVER = "ftp.heanet.ie"
FTP_FILE   = "pub/download.sourceforge.net/pub/sourceforge/e/ed/edk2/OVMF/" & OVMF_ZIP
FTP_URL    = "ftp://" & FTP_SERVER & "/" & FTP_FILE
VHD_ZIP    = "ntfs.zip"
VHD_IMG    = "ntfs.vhd"
VHD_URL    = "http://efi.akeo.ie/test/" & VHD_ZIP
DRV        = "ntfs_x64.efi"
DRV_URL    = "http://efi.akeo.ie/downloads/efifs-0.6.1/x64/" & DRV

' Globals
Set fso = CreateObject("Scripting.FileSystemObject") 
Set shell = CreateObject("WScript.Shell")

' Download a file from FTP
Sub DownloadFtp(Server, Path)
  Set file = fso.CreateTextFile("ftp.txt", True)
  Call file.Write("open " & Server & vbCrLf &_
    "anonymous" & vbCrLf & "user" & vbCrLf & "bin" & vbCrLf &_
    "get " & Path & vbCrLf & "bye" & vbCrLf)
  Call file.Close()
  Call shell.Run("%comspec% /c ftp -s:ftp.txt > NUL", 0, True)
  Call fso.DeleteFile("ftp.txt")
End Sub

' Download a file from HTTP
Sub DownloadHttp(Url, File)
  Const BINARY = 1
  Const OVERWRITE = 2
  Set xHttp = createobject("Microsoft.XMLHTTP")
  Set bStrm = createobject("Adodb.Stream")
  Call xHttp.Open("GET", Url, False)
  Call xHttp.Send()
  With bStrm
    .type = BINARY
    .open
    .write xHttp.responseBody
    .savetofile File, OVERWRITE
  End With
End Sub

' Unzip a specific file from an archive
Sub Unzip(Archive, File)
  Const NOCONFIRMATION = &H10&
  Const NOERRORUI = &H400&
  Const SIMPLEPROGRESS = &H100&
  unzipFlags = NOCONFIRMATION + NOERRORUI + SIMPLEPROGRESS
  Set objShell = CreateObject("Shell.Application")
  Set objSource = objShell.NameSpace(fso.GetAbsolutePathName(Archive)).Items()
  Set objTarget = objShell.NameSpace(fso.GetAbsolutePathName("."))
  ' Only extract the file we are interested in
  For i = 0 To objSource.Count - 1
    If objSource.Item(i).Name = File Then
      Call objTarget.CopyHere(objSource.Item(i), unzipFlags)
    End If
  Next
End Sub

' Fetch the Tianocore UEFI BIOS and unzip it
If Not fso.FileExists(OVMF_BIOS) Then
  Call WScript.Echo("The latest OVMF BIOS file, needed for QEMU/EFI, " &_
   "will be downloaded from: " & FTP_URL & vbCrLf & vbCrLf &_
   "Note: Unless you delete the file, this should only happen once.")
  Call DownloadFtp(FTP_SERVER, FTP_FILE)
  Call Unzip(OVMF_ZIP, OVMF_BIOS)
  Call fso.DeleteFile(OVMF_ZIP)
End If

' Fetch the NTFS VHD image
If Not fso.FileExists(VHD_IMG) Then
  Call DownloadHttp(VHD_URL, VHD_ZIP)
  Call Unzip(VHD_ZIP, VHD_IMG)
  Call fso.DeleteFile(VHD_ZIP)
End If

' Fetch the NTFS EFI driver
If Not fso.FileExists(DRV) Then
  Call DownloadHttp(DRV_URL, DRV)
End If

' Copy the files where required, and start QEMU
Call shell.Run("%COMSPEC% /c mkdir ""image\\efi\\boot""", 0, True)
Call fso.CopyFile(WScript.Arguments(0), "image\\efi\\boot\\bootx64.efi", True)
Call shell.Run("%COMSPEC% /c mkdir ""image\\efi\\rufus""", 0, True)
Call fso.CopyFile(DRV, "image\\efi\\rufus\\" & DRV, True)
Call shell.Run("""C:\\Program Files\\qemu\\qemu-system-x86_64w.exe"" -L . -bios OVMF.fd -net none -hda fat:image -hdb ntfs.vhd", 1, True)
