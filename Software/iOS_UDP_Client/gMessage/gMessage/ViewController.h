//
//  ViewController.h
//  gMessage
//
//  Created by Walter Greger on 26.02.19.
//  Copyright © 2019 Walter Greger. All rights reserved.
//

#import <UIKit/UIKit.h>
#import "GCDAsyncUdpSocket.h"

@interface ViewController : UIViewController <GCDAsyncUdpSocketDelegate> {
    
}

@property(nonatomic,retain) GCDAsyncUdpSocket* udpSocket;
@property(nonatomic,retain) IBOutlet UITextField* messageField;
@property(nonatomic,retain) IBOutlet UIImageView* transmissionStatus;


- (void)sendMessageviaUDP:(NSString*)_message;
- (IBAction)pressedSendButton:(id)sender;
-(void)transmissionFailed;

@end

