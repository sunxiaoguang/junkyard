package main

import (
	"flag"
	"fmt"
	"time"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/client"
	"github.com/aws/aws-sdk-go/aws/session"
	"github.com/aws/aws-sdk-go/service/sqs"
)

var (
	ssoProfile        = flag.String("sso-profile", "sso-profile", "The name of the SSO profile to use")
	region            = flag.String("region", "us-east-1", "The AWS region to use")
	queueURL          = flag.String("queue-url", "", "The URL of the SQS queue to receive messages from")
	maxBatchSize      = flag.Uint("max-batch-size", 10, "The maximum number of messages to receive in a single batch")
	maxWaitTime       = flag.Duration("max-wait-time", 5*time.Second, "The maximum amount of time to wait for messages")
	visibilityTimeout = flag.Duration("visibility-timeout", 60*time.Second, "The visibility timeout for messages")
)

type BatchSQSMessageConsumer struct {
	maxWaitTime              time.Duration
	maxBatchSize             uint32
	visibilityTimeoutSeconds int64
	queueURL                 string
	svc                      *sqs.SQS
}

func (b *BatchSQSMessageConsumer) ReceiveBatch() (batch []*sqs.Message, err error) {
	batch = make([]*sqs.Message, 0, b.maxBatchSize)

	start := time.Now()
	deadline := start.Add(b.maxWaitTime)

	maxNumberOfMessages := int64(b.maxBatchSize)
	if b.maxBatchSize > 10 {
		maxNumberOfMessages = 10
	}

	for time.Now().Before(deadline) && len(batch) < int(b.maxBatchSize) {
		result, err := b.svc.ReceiveMessage(&sqs.ReceiveMessageInput{
			AttributeNames: []*string{
				aws.String(sqs.MessageSystemAttributeNameAll),
			},
			MessageAttributeNames: []*string{
				aws.String(sqs.QueueAttributeNameAll),
			},
			QueueUrl:            &b.queueURL,
			MaxNumberOfMessages: &maxNumberOfMessages,
			WaitTimeSeconds:     aws.Int64(int64(deadline.Sub(time.Now()).Seconds())),
			VisibilityTimeout:   &b.visibilityTimeoutSeconds,
		})
		if err != nil {
			return batch, err
		}

		if result.Messages != nil {
			batch = append(batch, result.Messages...)
		}
	}
	return batch, nil
}

func (b *BatchSQSMessageConsumer) deleteMessagesStep(messages []*sqs.Message) error {
	entries := make([]*sqs.DeleteMessageBatchRequestEntry, 0, len(messages))
	for _, message := range messages {
		entries = append(entries, &sqs.DeleteMessageBatchRequestEntry{
			Id:            message.MessageId,
			ReceiptHandle: message.ReceiptHandle,
		})
	}

	_, err := b.svc.DeleteMessageBatch(&sqs.DeleteMessageBatchInput{
		QueueUrl: &b.queueURL,
		Entries:  entries,
	})
	return err
}

func (b *BatchSQSMessageConsumer) DeleteBatch(batch []*sqs.Message) ([]*sqs.Message, error) {
	for len(batch) > 10 {
		err := b.deleteMessagesStep(batch[:10])
		if err != nil {
			return batch, err
		}
		batch = batch[10:]
	}
	if len(batch) > 0 {
		err := b.deleteMessagesStep(batch)
		if err != nil {
			return batch, err
		}
	}
	return nil, nil
}

func NewBatchSQSMessageConsumer(configProvider client.ConfigProvider, queueURL string, maxBatchSize uint32, maxWaitTime time.Duration, visibilityTimeout time.Duration) *BatchSQSMessageConsumer {
	return &BatchSQSMessageConsumer{
		maxWaitTime:              maxWaitTime,
		maxBatchSize:             maxBatchSize,
		queueURL:                 queueURL,
		visibilityTimeoutSeconds: int64(visibilityTimeout.Seconds()),
		svc:                      sqs.New(configProvider),
	}
}

func main() {
	sess := session.Must(session.NewSessionWithOptions(session.Options{
		SharedConfigState: session.SharedConfigEnable,
		Profile:           *ssoProfile,
	}))
	sess.Config.Region = aws.String(*region)

	batchConsumer := NewBatchSQSMessageConsumer(sess, *queueURL, uint32(*maxBatchSize), *maxWaitTime, *visibilityTimeout)

	for {
		messages, err := batchConsumer.ReceiveBatch()
		if err != nil {
			fmt.Println("Error receiving messages:", err)
			return
		}

		if len(messages) == 0 {
			fmt.Println("Received no messages")
			return
		}
		fmt.Printf("Received %d messages.\n", len(messages))

		for _, message := range messages {
			fmt.Println("ID:     " + *message.MessageId)
			fmt.Println("Body:   " + *message.Body)
			fmt.Println("Handle: " + *message.ReceiptHandle)
			fmt.Println("Group ID: " + *message.Attributes[sqs.MessageSystemAttributeNameMessageGroupId])
			fmt.Println("Deduplication ID: " + *message.Attributes[sqs.MessageSystemAttributeNameMessageDeduplicationId])
		}

		failed, err := batchConsumer.DeleteBatch(messages)
		if err != nil {
			fmt.Println("Delete Error", failed, err)
			return
		}
		fmt.Println("Deleted messages.")
	}
}
