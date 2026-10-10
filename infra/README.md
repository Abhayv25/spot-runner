# Deploying to AWS

The Terraform in `terraform/` creates everything the manager needs:

- an S3 bucket for checkpoints and results (private, encrypted, 30-day expiry)
- an SQS job queue and dead-letter queue
- ECR repositories for the manager and job images
- a least-privilege IAM role and instance profile for workers
- a launch template (Amazon Linux 2023, IMDSv2 required) and an Auto Scaling
  group that is 100% Spot, spread over six instance types with the
  `price-capacity-optimized` strategy and Capacity Rebalancing enabled
- an AWS Fault Injection Service template that sends a real two-minute Spot
  interruption notice to one worker

The group starts at zero instances, so applying the Terraform costs almost
nothing until you scale it up.

## Prerequisites

- An AWS account with a budget alert configured
- AWS CLI v2, authenticated (`aws sts get-caller-identity` works)
- Terraform 1.6 or newer
- Docker with buildx

## Steps

```bash
cd infra/terraform
terraform init
terraform apply

REGION=us-east-1
REGISTRY=$(aws sts get-caller-identity --query Account --output text).dkr.ecr.$REGION.amazonaws.com
aws ecr get-login-password --region $REGION | docker login --username AWS --password-stdin $REGISTRY

# Build for x86_64 even from an Apple Silicon Mac.
docker buildx build --platform linux/amd64 -t $REGISTRY/prime-counter:latest --push ../../jobs/prime-counter
docker buildx build --platform linux/amd64 -t $(terraform output -raw manager_repository_url):latest --push ../..

# Queue some work. The enqueue command needs a config pointing at the AWS queue;
# fill config/aws.example.json from `terraform output` and build with the aws preset.
spot-runner enqueue --config ../../config/aws.json ../../examples/jobs/*.json

# Start two workers.
aws autoscaling set-desired-capacity --auto-scaling-group-name $(terraform output -raw autoscaling_group) --desired-capacity 2

# Send a real Spot interruption to one of them.
aws fis start-experiment --experiment-template-id $(terraform output -raw fis_template_id)
```

Watch the event log on a worker through Session Manager
(`/var/log/spot-runner/events.jsonl`). You should see `interruption_notice`,
`job_stopping`, `checkpoint_uploaded`, and `job_released` on the interrupted
instance, followed by `job_claimed` with `"resumed": true` on the other one.

## Tearing down

```bash
aws autoscaling set-desired-capacity --auto-scaling-group-name $(terraform output -raw autoscaling_group) --desired-capacity 0
terraform destroy
```

## Notes

- **Image architecture.** The launch template uses x86_64 instance types. Images
  built on an Apple Silicon Mac must be built with `--platform linux/amd64`.
- **IMDS from a container.** The manager runs in a container with
  `--network host`, and the launch template sets the IMDSv2 hop limit to 2, so
  the manager can reach the metadata service for interruption notices and
  credentials.
- **Registry credentials.** The Docker Engine API ignores the CLI's stored
  credentials, so user data pre-pulls images with the CLI. For images that are
  not pre-pulled, set `docker.registry_auth_file` in the manager config to a
  file containing the registry auth JSON.
- **Status.** The Terraform is checked with `terraform validate` in CI. The AWS backends
  compile and link against AWS SDK for C++ 1.11.909. The end-to-end Spot
  interruption run above has not yet been performed against a live account.
