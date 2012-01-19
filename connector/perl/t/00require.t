#!/usr/bin/perl 
use warnings;
use strict;

use Test::More tests => 16; 

require_ok 'MR::IProto'; 
require_ok 'MR::IProto::Cluster'; 
require_ok 'MR::IProto::Cluster::Server'; 
require_ok 'MR::IProto::Connection'; 
require_ok 'MR::IProto::Connection::Async'; 
require_ok 'MR::IProto::Connection::Sync'; 
require_ok 'MR::IProto::Error'; 
require_ok 'MR::IProto::Message'; 
require_ok 'MR::IProto::NoResponse'; 
require_ok 'MR::IProto::Request'; 
require_ok 'MR::IProto::Response'; 
require_ok 'MR::IProto::Role::Debuggable'; 
require_ok 'MR::IProto::Server'; 
require_ok 'MR::IProto::Server::Connection'; 
require_ok 'MR::Tarantool::Box'; 
require_ok 'MR::Tarantool::Box::Singleton'; 

