# --- Checkpoint and result storage -------------------------------------------

resource "aws_s3_bucket" "checkpoints" {
  bucket_prefix = "${var.name}-checkpoints-"
  force_destroy = true # demo project: let `terraform destroy` remove objects too
}

resource "aws_s3_bucket_public_access_block" "checkpoints" {
  bucket                  = aws_s3_bucket.checkpoints.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

resource "aws_s3_bucket_server_side_encryption_configuration" "checkpoints" {
  bucket = aws_s3_bucket.checkpoints.id
  rule {
    apply_server_side_encryption_by_default {
      sse_algorithm = "AES256"
    }
  }
}

resource "aws_s3_bucket_lifecycle_configuration" "checkpoints" {
  bucket = aws_s3_bucket.checkpoints.id
  rule {
    id     = "expire-old-job-data"
    status = "Enabled"
    filter {
      prefix = "spot-runner/"
    }
    expiration {
      days = 30
    }
  }
}

# --- Job queue ----------------------------------------------------------------

resource "aws_sqs_queue" "dead_letter" {
  name                      = "${var.name}-dead-letter"
  message_retention_seconds = 1209600 # 14 days, the SQS maximum
}

resource "aws_sqs_queue" "jobs" {
  name                       = "${var.name}-jobs"
  visibility_timeout_seconds = var.lease_duration_s
  receive_wait_time_seconds  = 10     # long polling
  message_retention_seconds  = 345600 # 4 days

  # The manager dead-letters crashing jobs itself, counting only real failures
  # (interruptions do not count). This redrive policy is a backstop for poison
  # messages that kill the manager before it can count anything. The threshold
  # is high because every interruption also increments the receive count.
  redrive_policy = jsonencode({
    deadLetterTargetArn = aws_sqs_queue.dead_letter.arn
    maxReceiveCount     = 50
  })
}

# --- Container images ---------------------------------------------------------

resource "aws_ecr_repository" "manager" {
  name                 = "${var.name}-manager"
  image_tag_mutability = "MUTABLE"
  force_delete         = true
  image_scanning_configuration {
    scan_on_push = true
  }
}

resource "aws_ecr_repository" "prime_counter" {
  name                 = "prime-counter"
  image_tag_mutability = "MUTABLE"
  force_delete         = true
  image_scanning_configuration {
    scan_on_push = true
  }
}

resource "aws_ecr_lifecycle_policy" "keep_recent" {
  for_each   = { manager = aws_ecr_repository.manager.name, job = aws_ecr_repository.prime_counter.name }
  repository = each.value
  policy     = jsonencode({
    rules = [{
      rulePriority = 1
      description  = "Keep the 10 most recent images"
      selection    = { tagStatus = "any", countType = "imageCountMoreThan", countNumber = 10 }
      action       = { type = "expire" }
    }]
  })
}
