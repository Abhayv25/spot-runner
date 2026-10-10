data "aws_caller_identity" "current" {}

data "aws_vpc" "default" {
  default = true
}

data "aws_subnets" "default" {
  filter {
    name   = "vpc-id"
    values = [data.aws_vpc.default.id]
  }
}

data "aws_ssm_parameter" "al2023" {
  name = "/aws/service/ami-amazon-linux-latest/al2023-ami-kernel-default-x86_64"
}

locals {
  registry = "${data.aws_caller_identity.current.account_id}.dkr.ecr.${var.region}.amazonaws.com"
}

# Workers need outbound access only (ECR, S3, SQS, SSM); nothing listens.
resource "aws_security_group" "worker" {
  name_prefix = "${var.name}-worker-"
  vpc_id      = data.aws_vpc.default.id

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

resource "aws_launch_template" "worker" {
  name_prefix   = "${var.name}-worker-"
  image_id      = data.aws_ssm_parameter.al2023.value
  instance_type = var.instance_types[0]

  iam_instance_profile {
    arn = aws_iam_instance_profile.worker.arn
  }

  vpc_security_group_ids = [aws_security_group.worker.id]

  metadata_options {
    http_tokens = "required" # IMDSv2 only
    # The manager runs in a container; one extra network hop lets it reach IMDS.
    http_put_response_hop_limit = 2
    http_endpoint               = "enabled"
  }

  user_data = base64encode(templatefile("${path.module}/user_data.sh.tftpl", {
    region                = var.region
    registry              = local.registry
    manager_image         = "${aws_ecr_repository.manager.repository_url}:${var.manager_image_tag}"
    job_images            = [for image in var.job_images : "${local.registry}/${image}"]
    bucket                = aws_s3_bucket.checkpoints.bucket
    queue_url             = aws_sqs_queue.jobs.url
    dead_letter_queue_url = aws_sqs_queue.dead_letter.url
    lease_duration_s      = var.lease_duration_s
  }))

  tag_specifications {
    resource_type = "instance"
    tags = {
      Name          = "${var.name}-worker"
      "spot-runner" = "worker" # FIS targets instances by this tag
    }
  }
}

resource "aws_autoscaling_group" "workers" {
  name_prefix         = "${var.name}-workers-"
  min_size            = 0
  max_size            = var.max_size
  desired_capacity    = var.desired_capacity
  vpc_zone_identifier = data.aws_subnets.default.ids

  # Launch a replacement as soon as EC2 signals elevated interruption risk.
  capacity_rebalance = true

  mixed_instances_policy {
    instances_distribution {
      on_demand_base_capacity                  = 0
      on_demand_percentage_above_base_capacity = 0 # 100% Spot
      spot_allocation_strategy                 = "price-capacity-optimized"
    }

    launch_template {
      launch_template_specification {
        launch_template_id = aws_launch_template.worker.id
        version            = "$Latest"
      }
      dynamic "override" {
        for_each = var.instance_types
        content {
          instance_type = override.value
        }
      }
    }
  }
}
