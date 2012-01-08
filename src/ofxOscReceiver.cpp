/*
 
 Copyright (c) 2007-2009, Damian Stewart
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 * Neither the name of the developer nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY DAMIAN STEWART ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL DAMIAN STEWART BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ofxOscReceiver.h"

#ifndef TARGET_WIN32
        #include <pthread.h>
#endif
#include <iostream>
#include <assert.h>

ofxOscReceiver::ofxOscReceiver()
{
	listen_socket = NULL;
}

void ofxOscReceiver::setup( int listen_port )
{
	// if we're already running, shutdown before running again
	if ( listen_socket )
		shutdown();
	
	// create the mutex
	#ifdef TARGET_WIN32
	mutex = CreateMutexA( NULL, FALSE, NULL );
	#else
	pthread_mutex_init( &mutex, NULL );
	#endif
	
	// create socket
	socketHasShutdown = false;
	listen_socket = new UdpListeningReceiveSocket( IpEndpointName( IpEndpointName::ANY_ADDRESS, listen_port ), this );

	// start thread
	#ifdef TARGET_WIN32
	thread	= CreateThread(
							   NULL,              // default security attributes
							   0,                 // use default stack size
							&ofxOscReceiver::startThread,        // thread function
							   (void*)this,             // argument to thread function
							   0,                 // use default creation flags
							   NULL);             // we don't the the thread id

	#else
	pthread_create( &thread, NULL, &ofxOscReceiver::startThread, (void*)this );
	#endif
    
    ofAddListener(ofEvents.update, this, &ofxOscReceiver::update);
}

void ofxOscReceiver::shutdown()
{
	if ( listen_socket )
	{
		// tell the socket to shutdown
		listen_socket->AsynchronousBreak();
		// wait for shutdown to complete
		while (!socketHasShutdown)
		{
			#ifdef TARGET_WIN32
			Sleep(1);
			#else
			// sleep 0.1ms
			usleep(100);
			#endif
		}
		
		// thread will clean up itself
		
		// clean up the mutex
		#ifdef TARGET_WIN32
		ReleaseMutex( mutex );
		#else
		pthread_mutex_destroy( &mutex );	
		#endif
		
		// delete the socket
		delete listen_socket;
		listen_socket = NULL;
	}
}

ofxOscReceiver::~ofxOscReceiver()
{
	shutdown();
}

#ifdef TARGET_WIN32
DWORD WINAPI
#else
void*
#endif
		ofxOscReceiver::startThread( void* receiverInstance )
{
	// cast the instance
	ofxOscReceiver* instance = (ofxOscReceiver*)receiverInstance;
	// start the socket listener
	instance->listen_socket->Run();
	// socket listener has finished - remember this fact
	instance->socketHasShutdown = true;
	// return
    #ifdef TARGET_WIN32
	return 0;
    #else
	return NULL;
    #endif
}

void ofxOscReceiver::ProcessMessage( const osc::ReceivedMessage &m, const IpEndpointName& remoteEndpoint )
{
	// convert the message to an ofxOscMessage
	ofxOscMessage* ofMessage = new ofxOscMessage();

	// set the address
	ofMessage->setAddress( m.AddressPattern() );

	// set the sender ip/host
	char endpoint_host[ IpEndpointName::ADDRESS_STRING_LENGTH ];
	remoteEndpoint.AddressAsString( endpoint_host );
    ofMessage->setRemoteEndpoint( endpoint_host, remoteEndpoint.port );

	// transfer the arguments
	for ( osc::ReceivedMessage::const_iterator arg = m.ArgumentsBegin();
		  arg != m.ArgumentsEnd();
		  ++arg )
	{
		if ( arg->IsInt32() )
			ofMessage->addIntArg( arg->AsInt32Unchecked() );
		else if ( arg->IsFloat() )
			ofMessage->addFloatArg( arg->AsFloatUnchecked() );
		else if ( arg->IsString() )
			ofMessage->addStringArg( arg->AsStringUnchecked() );
		else
		{
			cout << "message argument is not int, float, or string" << endl;
		}
	}

	// now add to the queue

	// at this point we are running inside the thread created by startThread,
	// so anyone who calls hasWaitingMessages() or getNextMessage() is coming
	// from a different thread

	// so we have to practise shared memory management

	// grab a lock on the queue
	grabMutex();

	// add incoming message on to the queue
	messages.push_back( ofMessage );

	// release the lock
	releaseMutex();
}

bool ofxOscReceiver::hasWaitingMessages()
{
	// grab a lock on the queue
	grabMutex();

	// check the length of the queue
	int queue_length = (int)messages.size();

	// release the lock
	releaseMutex();

	// return whether we have any messages
	return queue_length > 0;
}

bool ofxOscReceiver::getNextMessage( ofxOscMessage* message )
{
	// grab a lock on the queue
	grabMutex();

	// check if there are any to be got
	if ( messages.size() == 0 )
	{
		// no: release the mutex
		releaseMutex();
		return false;
	}

	// copy the message from the queue to message
	ofxOscMessage* src_message = messages.front();
	message->copy( *src_message );

	// now delete the src message
	delete src_message;
	// and remove it from the queue
	messages.pop_front();

	// release the lock on the queue
	releaseMutex();

	// return success
	return true;
}

void ofxOscReceiver::grabMutex()
{
#ifdef TARGET_WIN32
	WaitForSingleObject( mutex, INFINITE );
#else
	pthread_mutex_lock( &mutex );
#endif
}

void ofxOscReceiver::releaseMutex()
{
#ifdef TARGET_WIN32
	ReleaseMutex( mutex );
#else
	pthread_mutex_unlock( &mutex );
#endif
}

void ofxOscReceiver::update(ofEventArgs &e)
{
    ofxOscMessage msg;
    if (getNextMessage(&msg))
    {
        ofNotifyEvent(onMessageReceived, msg, this);
    }
}
