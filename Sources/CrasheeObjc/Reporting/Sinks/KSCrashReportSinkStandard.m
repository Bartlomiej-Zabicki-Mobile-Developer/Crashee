//
//  KSCrashReportSinkStandard.m
//
//  Created by Karl Stenerud on 2012-02-18.
//
//  Copyright (c) 2012 Karl Stenerud. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall remain in place
// in this source code.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//


#import "KSCrashReportSinkStandard.h"

#import "../Tools/KSHTTPMultipartPostBody.h"
#import "../Tools/KSHTTPRequestSender.h"
#import "../Filters/Tools/NSData+GZip.h"
#import "../../Recording/Tools/KSJSONCodecObjC.h"
#import "../Tools/KSReachabilityKSCrash.h"
#import "../../Recording/Tools/NSError+SimpleConstructor.h"
#import "../Filters/KSCrashReportFilterAppleFmt.h"
#import "../Filters/KSCrashReportFilterBasic.h"
#import "../Filters/KSCrashReportFilterGZip.h"

//#define KSLogger_LocalLevel TRACE
#import "../../Recording/Tools/KSLogger.h"


@interface KSCrashReportSinkStandard ()

@property(nonatomic,readwrite,retain) NSURL* url;

@property(nonatomic,readwrite,retain) KSReachableOperationKSCrash* reachableOperation;


@end


@implementation KSCrashReportSinkStandard

@synthesize url = _url;
@synthesize reachableOperation = _reachableOperation;

+ (KSCrashReportSinkStandard*) sinkWithURL:(NSURL*) url
{
    return [[self alloc] initWithURL:url];
}

- (id) initWithURL:(NSURL*) url
{
    if((self = [super init]))
    {
        self.url = url;
    }
    return self;
}

- (id <KSCrashReportFilter>) defaultCrashReportFilterSet
{
    return self;
}
- (id <KSCrashReportFilter>) defaultCrashReportFilterSetAppleFmt
{
    return [KSCrashReportFilterPipeline filterWithFilters:
            [KSCrashReportFilterAppleFmt filterWithReportStyle:KSAppleReportStyleSymbolicatedSideBySide],
            [KSCrashReportFilterStringToData filter],
            [KSCrashReportFilterGZipCompress filterWithCompressionLevel:-1],
            self,
            nil];
}


- (void) filterReports:(NSArray*) reports
          onCompletion:(KSCrashReportFilterCompletion) onCompletion
{
    NSError* error = nil;
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:self.url
                                                           cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                       timeoutInterval:15];
    
    NSString *BoundaryConstant = @"----------V2ymHFg03ehbqgZCaKO6jy";
    NSString *contentType = [NSString stringWithFormat:@"multipart/form-data; boundary=%@", BoundaryConstant];
    [request setValue:contentType forHTTPHeaderField: @"Content-Type"];

    NSMutableData *body = [NSMutableData data];
    
    for (int i=0; i<[reports count]; i++)
    {
        NSString*filename=[NSString stringWithFormat:@"report%d.json",i+1];

        NSString* FileParamConstant = [NSString stringWithFormat:@"reports[]"];

        NSData *reportData = reports[i];

        if (reportData)
        {
            [body appendData:[[NSString stringWithFormat:@"\r\n--%@\r\n", BoundaryConstant] dataUsingEncoding:NSUTF8StringEncoding]];
            [body appendData:[[NSString stringWithFormat:@"Content-Disposition: form-data; name=\"%@\"; filename=\"%@\"\r\n", FileParamConstant,filename] dataUsingEncoding:NSUTF8StringEncoding]];
            [body appendData:[@"Content-Type: json\r\n\r\n" dataUsingEncoding:NSUTF8StringEncoding]];
            NSData* jsonData = [KSJSONCodec encode:reportData
            options:KSJSONEncodeOptionSorted
              error:&error];
            [body appendData:jsonData];
        }

    }
    [body appendData:[[NSString stringWithFormat:@"\r\n--%@\r\n", BoundaryConstant] dataUsingEncoding:NSUTF8StringEncoding]];

    request.HTTPMethod = @"POST";
    request.HTTPBody = body;
    [request setValue:@"CrashReporter" forHTTPHeaderField:@"User-Agent"];

    self.reachableOperation = [KSReachableOperationKSCrash operationWithHost:[self.url host]
                                                                   allowWWAN:YES
                                                                       block:^
    {
        [[KSHTTPRequestSender sender] sendRequest:request
                                        onSuccess:^(__unused NSHTTPURLResponse* response, __unused NSData* data)
         {
             kscrash_callCompletion(onCompletion, reports, YES, nil);
         } onFailure:^(NSHTTPURLResponse* response, NSData* data)
         {
             NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
             kscrash_callCompletion(onCompletion, reports, NO,
                                    [NSError errorWithDomain:[[self class] description]
                                                        code:response.statusCode
                                                    userInfo:[NSDictionary dictionaryWithObject:text
                                                                                         forKey:NSLocalizedDescriptionKey]
                                     ]);
         } onError:^(NSError* error2)
         {
             kscrash_callCompletion(onCompletion, reports, NO, error2);
         }];
    }];
}

@end
