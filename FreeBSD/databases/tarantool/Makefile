# Created by: Veniamin Gvozdikov <vg@FreeBSD.org>
# $FreeBSD$

PORTNAME=	tarantool
PORTVERSION=	1.6.5
CATEGORIES=	databases
MASTER_SITES=	http://tarantool.org/dist/master/
DISTNAME=	${PORTNAME}-${PORTVERSION}-${TR_REV}-src

MAINTAINER=	vg@FreeBSD.org
COMMENT=	High performance key/value storage server

LICENSE=	BSD2CLAUSE

TR_REV=		212-g49781b3
MAKE_JOBS_UNSAFE=yes
USES=		cmake compiler:c++11-lang gmake perl5 readline
USE_GCC=	4.9+
USE_LDCONFIG=	yes
USE_RC_SUBR=	${PORTNAME}
SUB_FILES=	pkg-message
CMAKE_ARGS=	-DCMAKE_INSTALL_MANDIR=man \
		-DCMAKE_INSTALL_SYSCONFDIR=${ETCDIR} \
		-DCMAKE_LOCALSTATE_DIR=${DATADIR}

OPTIONS_DEFINE=	DEBUG
DEBUG_DESC=	Enable debug

.include <bsd.port.options.mk>

.if ${PORT_OPTIONS:MDEBUG}
CMAKE_BUILD_TYPE=	RelWithDebugInfo
CMAKE_ARGS+=	-DENABLE_BACKTRACE=ON
.else
CMAKE_BUILD_TYPE=	Rel
CMAKE_ARGS+=	-DENABLE_BACKTRACE=OFF
.endif

.if ${OSVERSION} < 900000
BROKEN=		Does not build on FreeBSD < 9.0
.endif

.include <bsd.port.mk>
