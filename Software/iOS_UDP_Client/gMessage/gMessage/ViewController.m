//
//  ViewController.m
//  gMessage
//
//  Created by Walter Greger on 26.02.19.
//  Copyright © 2019 Walter Greger. All rights reserved.
//

#import "ViewController.h"

#define hostNameDisplay @"hsh.firewall-gateway.com" //the host name of the display, e.g. @"mydisplay.mydomain.com" or @"192.168.178.77"
#define udpPortDisplay 10002 //udp port of display which is defined in splitFlapMaster.ino, default 10002
#define udpReplyPort 44998 //udp reply port which is defined in splitFlapMaster.ino, default 44998
#define udpToken @"AH6715" //token for display which is defined in splitFlapMaster.ino
#define udpTimeout 5 //timeout for waiting of ACK in s, default is 5

@interface ViewController ()

@end

@implementation ViewController

NSTimer* transmissionFailedTimer;

@synthesize messageField,transmissionStatus,udpSocket;

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view, typically from a nib.
    self.udpSocket = [[GCDAsyncUdpSocket alloc] initWithDelegate:self delegateQueue:dispatch_get_main_queue()];
}

- (IBAction)pressedSendButton:(id)sender {
    NSString* messageString = self.messageField.text;
    NSString* token = udpToken;
    NSString* udpString = [NSString stringWithFormat:@"%@%@",token,messageString];
    [self sendMessageviaUDP:udpString];
}

- (void)sendMessageviaUDP:(NSString*)_message {
    self.transmissionStatus.image = nil;//delete old transmission status icon
    NSData *data = [_message dataUsingEncoding:NSUTF8StringEncoding];
    [udpSocket bindToPort:udpReplyPort error:nil];
    [udpSocket connectToHost:hostNameDisplay onPort:udpPortDisplay error:nil];
    [udpSocket sendData:data withTimeout:0 tag:0];
    NSError* anError;
    [udpSocket beginReceiving:&anError];
    if(anError) {
        NSLog(@"%@",anError.localizedDescription);
    }
    transmissionFailedTimer = [NSTimer scheduledTimerWithTimeInterval:udpTimeout target:self selector:@selector(transmissionFailed) userInfo:nil repeats:NO];
}
                    
- (void)udpSocket:(GCDAsyncUdpSocket *)sock didReceiveData:(NSData *)data fromAddress:(NSData *)address withFilterContext:(nullable id)filterContext {
    NSString* answer = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    NSLog(@"received answer:%@",answer);
    if([answer isEqualToString:@"ACK"]) {
        self.transmissionStatus.image = [UIImage imageNamed:@"success.png"];
        [transmissionFailedTimer invalidate];
    }
}

- (void)udpSocket:(GCDAsyncUdpSocket *)sock didConnectToAddress:(NSData *)address {
    NSLog(@"connected to  %@",address);
}

- (void)udpSocket:(GCDAsyncUdpSocket *)sock didSendDataWithTag:(long)tag {
    NSLog(@"send Data");
}

-(void)transmissionFailed {
    self.transmissionStatus.image = [UIImage imageNamed:@"failed.png"];
    NSLog(@"failed");
}


@end
