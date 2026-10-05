// =============================================================================
// sqs_queue.cpp: JobQueue on Amazon SQS (PHASE 7)
// =============================================================================
// Map each interface function to an SQS action:
//   push          -> SendMessage           (body = job_to_json(job))
//   receive       -> ReceiveMessage        (MaxNumberOfMessages=1,
//                                           VisibilityTimeout=lease_seconds,
//                                           WaitTimeSeconds=10  <- "long polling": waits
//                                           up to 10 s for a message instead of
//                                           returning empty instantly. Cheaper.)
//                    job.receipt = message.GetReceiptHandle()
//   extend_lease  -> ChangeMessageVisibility (receipt, lease_seconds)
//                    If SQS says the receipt is invalid -> throw "lease lost"
//   complete      -> DeleteMessage (receipt)
//   release       -> SQS can't edit a message. If count_as_failure is false:
//                    ChangeMessageVisibility(receipt, 0) = "visible again right now".
//                    If true: SendMessage a copy with attempts + 1, THEN DeleteMessage
//                    the old one (same "duplicate is safer than lost" order as LocalQueue).
//   dead_letter   -> Simplest: send to a second queue (the "DLQ" you create in
//                    infra/README.md), then DeleteMessage the original.
//                    (SQS can also do this automatically with a "redrive policy"; read
//                    about it and mention which you chose and why.)
//
// STEP 1: Includes
//   "spot_runner/sqs_queue.h", <aws/sqs/SQSClient.h>, and the model headers for each
//   action above (<aws/sqs/model/SendMessageRequest.h>, ReceiveMessageRequest,
//   DeleteMessageRequest, ChangeMessageVisibilityRequest),
//   <aws/core/client/ClientConfiguration.h>
//   using namespace std; namespace spot_runner { ... }
//
// STEP 2: Constructor / destructor: same pattern as S3Storage.
//
// STEP 3: Write each function using the mapping above. After every call:
//   if (!outcome.IsSuccess()) throw runtime_error(outcome.GetError().GetMessage());
//
// STEP 4: The DLQ needs its URL. Add "dlq_url" to AwsSettings in config.h and pass
//   it into this constructor.
// =============================================================================
