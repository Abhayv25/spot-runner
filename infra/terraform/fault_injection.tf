# AWS Fault Injection Service experiment that sends a real Spot interruption
# notice (the same two-minute warning AWS sends when it reclaims capacity) to
# one worker. Run it with:
#   aws fis start-experiment --experiment-template-id $(terraform output -raw fis_template_id)

data "aws_iam_policy_document" "fis_assume" {
  statement {
    actions = ["sts:AssumeRole"]
    principals {
      type        = "Service"
      identifiers = ["fis.amazonaws.com"]
    }
  }
}

resource "aws_iam_role" "fis" {
  name_prefix        = "${var.name}-fis-"
  assume_role_policy = data.aws_iam_policy_document.fis_assume.json
}

data "aws_iam_policy_document" "fis" {
  statement {
    actions   = ["ec2:SendSpotInstanceInterruptions"]
    resources = ["arn:aws:ec2:*:*:instance/*"]
    condition {
      test     = "StringEquals"
      variable = "aws:ResourceTag/spot-runner"
      values   = ["worker"]
    }
  }
  statement {
    actions   = ["ec2:DescribeInstances"]
    resources = ["*"]
  }
}

resource "aws_iam_role_policy" "fis" {
  name   = "send-spot-interruptions"
  role   = aws_iam_role.fis.id
  policy = data.aws_iam_policy_document.fis.json
}

resource "aws_fis_experiment_template" "spot_interruption" {
  description = "Interrupt one spot-runner worker with a two-minute notice"
  role_arn    = aws_iam_role.fis.arn

  action {
    name      = "interrupt-one-worker"
    action_id = "aws:ec2:send-spot-instance-interruptions"
    parameter {
      key   = "durationBeforeInterruption"
      value = "PT2M"
    }
    target {
      key   = "SpotInstances"
      value = "workers"
    }
  }

  target {
    name           = "workers"
    resource_type  = "aws:ec2:spot-instance"
    selection_mode = "COUNT(1)"
    resource_tag {
      key   = "spot-runner"
      value = "worker"
    }
    filter {
      path   = "State.Name"
      values = ["running"]
    }
  }

  stop_condition {
    source = "none"
  }
}
