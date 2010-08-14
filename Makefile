S?=			/usr/src/sys 
MKMAN=		no
KMOD?=		wimax
SRCS=		wimax_lkm.c
WARNS?=		0
CPPFLAGS+=	-nostdinc 
#COPTS?=
NOGCCERROR?=
#COPTS= 

.include <bsd.kmod.mk>

CPPFLAGS+=-I/usr/include/
