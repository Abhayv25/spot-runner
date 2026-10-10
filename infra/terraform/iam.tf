# Least-privilege role for the worker instances. The manager never sees access
# keys: the AWS SDK picks up temporary credentials for this role from IMDS.

data "aws_iam_policy_document" "ec2_assume" {
  statement {
    actions = ["sts:AssumeRole"]
    principals {
      type        = "Service"
      identifiers = ["ec2.amazonaws.com"]
    }
  }
}

resource "aws_iam_role" "worker" {
  name_prefix        = "${var.name}-worker-"
  assume_role_policy = data.aws_iam_policy_document.ec2_assume.json
}

data "aws_iam_policy_document" "worker" {
  statement {
    sid = "JobQueue"
    actions = [
      "sqs:ReceiveMessage",
      "sqs:SendMessage",
      "sqs:DeleteMessage",
      "sqs:ChangeMessageVisibility",
      "sqs:GetQueueAttributes",
    ]
    resources = [aws_sqs_queue.jobs.arn, aws_sqs_queue.dead_letter.arn]
  }

  statement {
    sid       = "CheckpointObjects"
    actions   = ["s3:GetObject", "s3:PutObject", "s3:DeleteObject"]
    resources = ["${aws_s3_bucket.checkpoints.arn}/spot-runner/*"]
  }

  # Without ListBucket, S3 answers GET/HEAD on a missing key with 403 instead of
  # 404, and "no checkpoint yet" would look like a permissions failure.
  statement {
    sid       = "CheckpointListing"
    actions   = ["s3:ListBucket"]
    resources = [aws_s3_bucket.checkpoints.arn]
    condition {
      test     = "StringLike"
      variable = "s3:prefix"
      values   = ["spot-runner/*"]
    }
  }
}

resource "aws_iam_role_policy" "worker" {
  name   = "spot-runner-worker"
  role   = aws_iam_role.worker.id
  policy = data.aws_iam_policy_document.worker.json
}

resource "aws_iam_role_policy_attachment" "ecr_read" {
  role       = aws_iam_role.worker.name
  policy_arn = "arn:aws:iam::aws:policy/AmazonEC2ContainerRegistryReadOnly"
}

# Shell access through Session Manager instead of SSH keys and open ports.
resource "aws_iam_role_policy_attachment" "ssm" {
  role       = aws_iam_role.worker.name
  policy_arn = "arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore"
}

resource "aws_iam_instance_profile" "worker" {
  name_prefix = "${var.name}-worker-"
  role        = aws_iam_role.worker.name
}
