SETV X=3
STRV NAME=llvm
LOGS INFO, direct llvm backend
FDIR MAKE,tmp_llvm
FILE WRITE,tmp_llvm/out.txt,hello llvm
FILE APPEND,tmp_llvm/out.txt, again
WAIT 1
