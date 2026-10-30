# Created by: Veniamin Gvozdikov <vg@FreeBSD.org>
# $FreeBSD$

PORTNAME=	tarantool
PORTVERSION=	1.6.8.371
CATEGORIES=	databases
MASTER_SITES=	http://download.tarantool.org/tarantool/1.6/src/

MAINTAINER=	vg@FreeBSD.org
COMMENT=	NoSQL database running in a Lua application server

LICENSE=	BSD2CLAUSE

MAKE_JOBS_UNSAFE=yes
USES=		cmake compiler:c++11-lang gettext gmake perl5 readline
USE_LDCONFIG=	yes
USE_RC_SUBR=	${PORTNAME}
SUB_FILES=	pkg-message

TT_DATADIR?=	/var/db/tarantool
TT_RUNDIR?=	/var/run/tarantool
TT_LOGDIR?=	/var/log/tarantool

CMAKE_ARGS=	-DCMAKE_INSTALL_MANDIR=man \
		-DCMAKE_INSTALL_LOCALSTATEDIR=/var \
		-DENABLE_DIST=ON

USERS=		tarantool
GROUPS=		tarantool

SUB_LIST+=	PORTNAME=${PORTNAME} \
	TT_USER=${USERS} \
	TT_DATADIR=${TT_DATADIR} \
	TT_LOGDIR=${TT_LOGDIR} \
	TT_RUNDIR=${TT_RUNDIR}

PLIST_SUB+=	TT_USER=${USERS} \
	TT_GROUP=${GROUPS} \
	TT_LOGDIR=${TT_LOGDIR} \
	TT_DATADIR=${TT_DATADIR} \
	TT_RUNDIR=${TT_RUNDIR}

OPTIONS_DEFINE=	DEBUG
DEBUG_DESC=	Enable debug

NOT_FOR_ARCHS=	sparc64
NOT_FOR_ARCHS_REASON=	not yet ported to this architecture

.include <bsd.port.options.mk>

.if ${PORT_OPTIONS:MDEBUG}
CMAKE_BUILD_TYPE=	RelWithDebugInfo
CMAKE_ARGS+=	-DENABLE_BACKTRACE=ON
.else
CMAKE_BUILD_TYPE=	Rel
CMAKE_ARGS+=	-DENABLE_BACKTRACE=OFF
.endif

.include <bsd.port.mk>
