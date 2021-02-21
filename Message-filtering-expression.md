* **Status**: "Under Discussion"
* **Author**: Xiaoguang Sun
* **Pull Request**: NA
* **Mailing List discussion**: NA
* **Release**: NA

### Motivation

Support filtering messages with flexible expressions.

Both consumer and reader can reset it's position to a specific message id or publish/event time, therefore can be used for time travel message processing. However there are scenarios where more sophisticated conditions can be useful, for example seeking to a particular message with property set to some specific value. MySQL binlog data or TiDB CDC events are two examples for these use cases. Applications can embed GTID or TSO into message property and use it to quickly seek to the most appropriate message when creating a streaming table from binlog. 

Compared to existing ways of seeking by position and timestamp, expression based seeking can be more flexible and easier to extend in the future to fulfill more sophisticated application scenarios. Not only is it beneficial to seeking messages, we could also add broker or client side message filtering for users with exactly the same feature. Therefore end users can be relieved from writing their own business logic to only process it's interested message from a topic.

### Public Interfaces
The following changes will be introduced to public interface:

- Client API

```java
public interface MessageFilterRef {
    static MessageFilterRef property(String key);
    static MessageFilterRef eventTime();
    static MessageFilterRef publishTime();
}

public enum MessageFilterOperator {
    EQUALS,
    NOT_EQUALS,
    IN,
    NOT_IN,
    GREATER_THAN,
    GREATER_THAN_EQUALS,
    LESS_THAN,
    LESS_THAN_EQUALS,
    STARTS_WITH,
    STARTS_WITH_IGNORE_CASE,
    CONTAINS,
    CONTAINS_IGNORE_CASE,
    DOES_NOT_CONTAIN, 
    DOES_NOT_CONTAIN_IGNORE_CASE, 
    CONTAINS_ANY,
    CONTAINS_NONE,
}

public interface MessageFilterExprBuilder {
    MessageFilterExprBuilder and(MessageFilterExprBuilder rhs, MessageFilterExprBuilder... others);
    MessageFilterExprBuilder or(MessageFilterExprBuilder rhs, MessageFilterExprBuilder... others);
    MessageFilterExprBuilder not();
    MessageFilterExpr build();
    
    static MessageFilterExprBuilder of(MessageFilterRef ref, MessageFilterOperator op, String rhs, String... others);
    static MessageFilterExprBuilder of(MessageFilterRef ref, MessageFilterOperator op, Long rhs, Long... others);
    static MessageFilterExprBuilder of(MessageFilterRef ref, MessageFilterOperator op, Double rhs, Double... others);
  
    static MessageFilterExprBuilder property(String key, MessageFilterOperator op, String rhs, String... others);
    static MessageFilterExprBuilder property(String key, MessageFilterOperator op, Long rhs, Long... others);
    static MessageFilterExprBuilder property(String key, MessageFilterOperator op, Double rhs, Double... others);
  
    static MessageFilterExprBuilder eventTime(MessageFilterRef ref, MessageFilterOperator op, Long rhs, Long... others);
    static MessageFilterExprBuilder publishTime(MessageFilterRef ref, MessageFilterOperator op, Long rhs, Long... others);
}
```

- The wire protocol

New message types to protobuf
```protobuf
message Predicate {
    enum MessageRef {
        PROPERTY = 0;
        EVENT_TIME = 1;
        PUBLISH_TIME = 2;
    }

    enum Operator {
        EQUALS = 0;
        NOT_EQUALS = 1;
        IN = 2;
        NOT_IN = 3;
        GREATER_THAN = 4;
        GREATER_THAN_EQUALS = 5;
        LESS_THAN = 6;
        LESS_THAN_EQUALS = 7;
        STARTS_WITH = 8;
        STARTS_WITH_IGNORE_CASE = 9;
        CONTAINS = 10;
        CONTAINS_IGNORE_CASE = 11;
        DOES_NOT_CONTAIN = 12;
        DOES_NOT_CONTAIN_IGNORE_CASE = 13;
        CONTAINS_ANY = 14;
        CONTAINS_NONE = 15;
    }
    
    enum ValueType {
        STRING = 0;
        LONG = 1;
        DOUBLE = 2;
    }

    required MessageRef ref = 1;
    optional string name = 2;
    required Operator op = 3;
    repeated string values = 4;
    optional ValueType ty = 5;
}

message Expr {
    enum ExprType {
        PREDICATE = 0;
        AND = 1;
        OR = 2;
        NOT = 3;
    }

    required ExprType tp = 1;
    optional Predicate predicate = 2;
    optional Expr op1 = 3;
    optional Expr op2 = 4;
}
```

Changes to existing message type

```protobuf
// Reset an existing consumer to a particular message id
message CommandSeek {
    required uint64 consumer_id = 1;
    required uint64 request_id  = 2;

    optional MessageIdData message_id = 3;
    optional uint64 message_publish_time = 4;
    optional Expr expression = 5;
}

```

### Proposed Changes

Add MessageFilterExprBuilder to help users build flexible expressions with a simple API. Users can specify and combine multiple predicates to properties of message or other metadata (event time and publish time at this time). With MessageFilterExpr created per user's request, people can specify where to start consuming messages with an overloaded seek method that takes a MessageFilterExpr as argument.

### Compatibility, Deprecation, and Migration Plan
Users can only access this feature with a new set of API therefore is compatible with existing code. The protobuf wire protocol changes are backward compatible to existing clients as well.

### Test Plan
Expression logic is self contained and has little external dependencies therefore easy to write comprehensive unit tests. The actual consumer and reader seek logic requires E2E test code to validate however.
