**To Initialize the Program**

***May need to chmod first:***
- chmod u+x generate-flags.sh
- chmod u+x generate-cmakelists.sh
- chmod u+x change-compiler.sh
- chmod u+x build.sh

***Compile the Program***
- ./generate-flags.sh
- ./generate-cmakelists.sh
- ./change-compiler.sh -c clang OR ./change-compiler.sh -c gcc
- ./build.sh
---
**Run the Program**
- __*NOTE: (IP Address may vary)*__


- Server side, to be entered in terminal:

  ./chat -a 192.168.0.5 6666


- Client side, to be entered in terminal:

  ./chat -c 192.168.0.5 6666


- After, start typing on either side.

***To Exit the program***
- Ctrl + D
- - - 
**Known Problems With Fixes**
- MallocNanoZone=0 encounter

***Fix***
- Open Terminal
- nano ./zshrc 
- Add this line: export MallocNanoZone=0
- - - 

**Authors**
- Wallace Trinh @ https://github.com/WallaceTrinh
- Jack Luo @ https://github.com/LzhJack
- - - 




