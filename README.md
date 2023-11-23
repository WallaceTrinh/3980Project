**To Initialize the Program**
- May need to chmod first:
- - chmod u+x generate-flags.sh
- - chmod u+x generate-cmakelists.sh
- - chmod u+x change-compiler -c clang OR ./generate change-compiler -c gcc
- - chmod u+x build.sh


- ./generate flags.sh
- ./generate cmakelists.sh
- ./change-compiler -c clang OR ./change-compiler -c gcc
- ./generate build.sh
---
**Run the Program**
- __*NOTE: (IP Address may vary)*__


- Server side, to be entered in terminal:

  export MallocNanoZone=0   
  ./chat -a 192.168.0.5 6666


- Client side, to be entered in terminal:

  export MallocNanoZone=0   
./chat -c 192.168.0.5 6666


- After, start typing on either side.


- To Exit the program
- - Ctrl + D
- - - 