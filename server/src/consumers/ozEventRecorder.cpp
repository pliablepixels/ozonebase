#include "../base/oz.h"
#include "ozEventRecorder.h"

#include "../base/ozMotionFrame.h"
#include "../base/ozNotifyFrame.h"
#include "../libgen/libgenTime.h"

/**
* @brief 
*
* @return 
*/
int EventRecorder::run()
{
    if ( waitForProviders() )
    {
        setReady();
        while( !mStop )
        {
            mQueueMutex.lock();
            if ( !mFrameQueue.empty() )
            {
                for ( FrameQueue::iterator iter = mFrameQueue.begin(); iter != mFrameQueue.end(); iter++ )
                {
                    processFrame( *iter );
                }
                mFrameQueue.clear();
            }
            mQueueMutex.unlock();
            checkProviders();
            usleep( INTERFRAME_TIMEOUT );
        }
    }
    FeedProvider::cleanup();
    FeedConsumer::cleanup();
    return( 0 );
}

/**
* @brief 
*
* @param frame
*
* @return 
*/
bool EventRecorder::processFrame( FramePtr frame )
{
    const AlarmFrame *alarmFrame = dynamic_cast<const AlarmFrame *>(frame.get());
    //const VideoProvider *provider = dynamic_cast<const VideoProvider *>(frame->provider());
    static uint64_t mLastAlarmTime;

    if ( !alarmFrame )
        return( false );

    AlarmState lastState = mState;

    if ( alarmFrame->alarmed() )
    {
        mState = ALARM;
        mLastAlarmTime = time64();
        if ( lastState == IDLE )
        {
            // Create new event
            mAlarmTime = mLastAlarmTime;
            mEventCount++;
            EventNotification::EventDetail detail( mEventCount, EventNotification::EventDetail::BEGIN );
            EventNotification *notification = new EventNotification( this, alarmFrame->id(), detail );
            distributeFrame( FramePtr( notification ) );
            for ( FrameStore::const_iterator iter = mFrameStore.begin(); iter != mFrameStore.end(); iter++ )
            {
                const AlarmFrame *frame = dynamic_cast<const AlarmFrame *>( iter->get() );
                if ( !frame )
                {
                    Error( "Unexpected frame type in frame store" );
                    continue;
                }
                std::string path = stringtf( "%s/img-%s-%d-%ju.jpg", mLocation.c_str(), mName.c_str(), mEventCount, frame->id() );
                //Info( "PF:%d @ %dx%d", frame->pixelFormat(), frame->width(), frame->height() );
                Image image( frame->pixelFormat(), frame->width(), frame->height(), frame->buffer().data() );
                image.writeJpeg( path.c_str() );
            }
        }
    }
    else if ( lastState == ALARM )
    {
        mState = ALERT;
    }

    if ( mState == ALERT )
    {
        if ( frame->age( mLastAlarmTime ) < -MAX_EVENT_TAIL_AGE )
        {
            mState = IDLE;
            EventNotification::EventDetail detail( mEventCount, ((double)mLastAlarmTime-mAlarmTime)/1000000.0 );
            EventNotification *notification = new EventNotification( this, alarmFrame->id(), detail );
            distributeFrame( FramePtr( notification ) );
        }
    }

    if ( mState > IDLE )
    {
        std::string path;
        if ( mState == ALARM )
        {
            path = stringtf( "%s/img-%s-%d-%ju-A.jpg", mLocation.c_str(), mName.c_str(), mEventCount, alarmFrame->id() );
        }
        else if ( mState == ALERT )
        {
            path = stringtf( "%s/img-%s-%d-%ju.jpg", mLocation.c_str(), mName.c_str(), mEventCount, alarmFrame->id() );
        }
        //Info( "PF:%d @ %dx%d", alarmFrame->pixelFormat(), alarmFrame->width(), alarmFrame->height() );
        Image image( alarmFrame->pixelFormat(), alarmFrame->width(), alarmFrame->height(), alarmFrame->buffer().data() );
        image.writeJpeg( path.c_str() );
    }

    // Clear out old frames
    Debug( 5, "Got %lu frames in store", mFrameStore.size() );
    while( !mFrameStore.empty() )
    {
        FramePtr tempFrame = *(mFrameStore.begin());
        Debug( 5, "Frame %ju age %.2lf", tempFrame->id(), tempFrame->age() );
        if ( tempFrame->age() <= MAX_EVENT_HEAD_AGE )
            break;
        Debug( 5, "Deleting" );
        //delete tempFrame;
        mFrameStore.pop_front();
    }
    mFrameStore.push_back( frame );
    mFrameCount++;
    return( true );
}
