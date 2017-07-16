# FUSE
<br>유저 공간에서 커널 코드를 건드리지 않고도 파일 시스템을 만들 수 있도록 도와주는 
<br>소프트웨어 인터페이스 FUSE(File System in User Space)를 사용하여 제작한 파일 시스템입니다.

Development environment
OS: Ubuntu v.15.04 
FUSE: v.26

Compile
-gcc -D_FILE_OFFSET_BITS=64 -o fsv01 fsv01.c -lfuse

Mount (마운트할 경로에 fsv01을 두고)
-./fsv01

Function
-storage init/load
-make/remove file/folder
-read/write file
-get attribute
-open file/folder
-chmod
등의 기능을 구현하였습니다. 명령어와 자세한 설명은 보고서를 참고하여 알 수 있습니다.
